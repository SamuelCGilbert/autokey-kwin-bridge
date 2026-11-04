/*
    SPDX-FileCopyrightText: 2026 AutoKey KWin Bridge contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "autokeybridge.h"

#include "keyboard_input.h"
#include "window.h"
#include "workspace.h"
#include "xkb.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>

#include <xkbcommon/xkbcommon.h>

#include <chrono>
#include <linux/input-event-codes.h>

namespace
{
const QString s_service = QStringLiteral("org.autokey.KWinBridge");
const QString s_path = QStringLiteral("/AutoKeyBridge");
const QString s_interface = QStringLiteral("org.autokey.KWinBridge");

enum Modifier : uint {
    Shift = 1,
    Control = 2,
    Alt = 4,
    Super = 8,
    AltGr = 16,
};
}

AutoKeyBridge::AutoKeyBridge()
    // Before KDE's global shortcuts, like XGrabKey on X11. The lock screen and
    // other higher priority filters still come first.
    : KWin::InputEventFilter(KWin::InputFilterOrder::TabBox)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerObject(s_path, this, QDBusConnection::ExportAllSlots);
    bus.registerService(s_service);

    m_watcher = new QDBusServiceWatcher(this);
    m_watcher->setConnection(bus);
    m_watcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &service) {
        if (service == m_client) {
            clientGone();
        }
    });

    m_monitorWatcher = new QDBusServiceWatcher(this);
    m_monitorWatcher->setConnection(bus);
    m_monitorWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_monitorWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &service) {
        m_monitors.remove(service);
        m_monitorWatcher->removeWatchedService(service);
    });

    m_motionTimer.setSingleShot(true);
    connect(&m_motionTimer, &QTimer::timeout, this, [this]() {
        if (!m_pendingMotion.isEmpty() && !m_monitors.isEmpty()) {
            sendToMonitors(QStringLiteral("MonitorMotion"), std::exchange(m_pendingMotion, {}));
        }
    });

    // Forget unplugged keyboards
    connect(KWin::input(), &KWin::InputRedirection::deviceRemoved, this, [this](KWin::InputDevice *device) {
        m_heldModifiers.remove(device);
        m_clearedModifiers.remove(device);
    });

    KWin::input()->installInputEventFilter(this);
}

AutoKeyBridge::~AutoKeyBridge()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterService(s_service);
    bus.unregisterObject(s_path);
}

QString AutoKeyBridge::Version() const
{
    return QStringLiteral("1");
}

void AutoKeyBridge::Register()
{
    if (!m_client.isEmpty()) {
        m_watcher->removeWatchedService(m_client);
    }
    m_client = message().service();
    m_watcher->addWatchedService(m_client);
    m_hotkeys.clear();
    m_grabbedKeys.clear();
}

void AutoKeyBridge::SetHotkeys(const QDBusMessage &msg)
{
    if (msg.service() != m_client) {
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Call Register() first"));
        return;
    }
    QList<AutoKeyHotkey> hotkeys;
    const QDBusArgument arg = msg.arguments().value(0).value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        AutoKeyHotkey hotkey;
        arg.beginStructure();
        arg >> hotkey.modifiers >> hotkey.keysym >> hotkey.filter;
        arg.endStructure();
        if (!hotkey.filter.isEmpty()) {
            hotkey.regex = QRegularExpression(hotkey.filter);
        }
        hotkeys.append(hotkey);
    }
    arg.endArray();
    m_hotkeys = hotkeys;
}

static bool isModifierScanCode(quint32 scanCode)
{
    switch (scanCode) {
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
    case KEY_LEFTMETA:
    case KEY_RIGHTMETA:
        return true;
    default:
        return false;
    }
}

static std::chrono::microseconds now()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

void AutoKeyBridge::ClearModifiers()
{
    for (auto it = m_heldModifiers.cbegin(); it != m_heldModifiers.cend(); ++it) {
        KWin::InputDevice *device = it.key();
        for (quint32 scanCode : it.value()) {
            if (m_clearedModifiers[device].contains(scanCode)) {
                continue;
            }
            m_clearedModifiers[device].insert(scanCode);
            m_emitting = true;
            Q_EMIT device->keyChanged(scanCode, KWin::KeyboardKeyState::Released, now(), device);
            m_emitting = false;
        }
    }
}

void AutoKeyBridge::RestoreModifiers()
{
    const auto cleared = std::exchange(m_clearedModifiers, {});
    for (auto it = cleared.cbegin(); it != cleared.cend(); ++it) {
        KWin::InputDevice *device = it.key();
        for (quint32 scanCode : it.value()) {
            // Only the ones that are still held down
            if (m_heldModifiers.value(device).contains(scanCode)) {
                m_emitting = true;
                Q_EMIT device->keyChanged(scanCode, KWin::KeyboardKeyState::Pressed, now(), device);
                m_emitting = false;
            }
        }
    }
}

void AutoKeyBridge::StartMonitor()
{
    const QString service = message().service();
    if (!m_monitors.contains(service)) {
        m_monitors.insert(service);
        m_monitorWatcher->addWatchedService(service);
    }
}

void AutoKeyBridge::StopMonitor()
{
    const QString service = message().service();
    m_monitors.remove(service);
    m_monitorWatcher->removeWatchedService(service);
}

void AutoKeyBridge::sendToMonitors(const QString &name, const QVariantList &arguments)
{
    // Keep the order: a delayed (throttled) motion goes out before anything else
    if (name != QLatin1String("MonitorMotion") && !m_pendingMotion.isEmpty()) {
        m_motionTimer.stop();
        sendToMonitors(QStringLiteral("MonitorMotion"), std::exchange(m_pendingMotion, {}));
    }
    for (const QString &monitor : std::as_const(m_monitors)) {
        QDBusMessage signal = QDBusMessage::createTargetedSignal(monitor, s_path, s_interface, name);
        signal.setArguments(arguments);
        QDBusConnection::sessionBus().send(signal);
    }
}

static uint buttonMask(Qt::MouseButtons buttons)
{
    uint mask = 0;
    if (buttons & Qt::LeftButton) {
        mask |= 1;
    }
    if (buttons & Qt::MiddleButton) {
        mask |= 2;
    }
    if (buttons & Qt::RightButton) {
        mask |= 4;
    }
    return mask;
}

bool AutoKeyBridge::pointerMotion(KWin::PointerMotionEvent *event)
{
    if (m_monitors.isEmpty() || event->warp || isSynthetic(event->device)) {
        return false;
    }
    // Pointer motion can come at 1000 Hz, recorders don't need that. The last
    // skipped position is sent shortly after, so the final one isn't lost.
    m_pendingMotion = {event->position.x(), event->position.y(), buttonMask(event->buttons), modifierMask(event->modifiers)};
    const qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(event->timestamp).count();
    if (now - m_lastMotion < 8) {
        if (!m_motionTimer.isActive()) {
            m_motionTimer.start(10);
        }
        return false;
    }
    m_lastMotion = now;
    m_motionTimer.stop();
    sendToMonitors(QStringLiteral("MonitorMotion"), std::exchange(m_pendingMotion, {}));
    return false;
}

bool AutoKeyBridge::pointerButton(KWin::PointerButtonEvent *event)
{
    if (!m_monitors.isEmpty() && !isSynthetic(event->device)) {
        sendToMonitors(QStringLiteral("MonitorButton"),
                       {uint(event->nativeButton), event->state == KWin::PointerButtonState::Pressed, event->position.x(),
                        event->position.y(), modifierMask(event->modifiers)});
    }
    return false;
}

bool AutoKeyBridge::pointerAxis(KWin::PointerAxisEvent *event)
{
    if (!m_monitors.isEmpty() && !isSynthetic(event->device)) {
        sendToMonitors(QStringLiteral("MonitorAxis"),
                       {event->orientation == Qt::Vertical, event->delta, int(event->deltaV120), event->position.x(),
                        event->position.y(), modifierMask(event->modifiers)});
    }
    return false;
}

void AutoKeyBridge::clientGone()
{
    m_watcher->removeWatchedService(m_client);
    m_client.clear();
    m_hotkeys.clear();
    m_grabbedKeys.clear();
}

uint AutoKeyBridge::levelKeysym(quint32 scanCode, int level) const
{
    KWin::Xkb *xkb = KWin::input()->keyboard()->xkb();
    if (!xkb || !xkb->keymap()) {
        return 0;
    }
    const xkb_keysym_t *syms = nullptr;
    const int count = xkb_keymap_key_get_syms_by_level(xkb->keymap(), scanCode + 8, xkb->currentLayout(), level, &syms);
    return count > 0 ? syms[0] : 0;
}

uint AutoKeyBridge::modifierMask(Qt::KeyboardModifiers modifiers) const
{
    uint mask = 0;
    if (modifiers & Qt::ShiftModifier) {
        mask |= Shift;
    }
    if (modifiers & Qt::ControlModifier) {
        mask |= Control;
    }
    if (modifiers & Qt::AltModifier) {
        mask |= Alt;
    }
    if (modifiers & Qt::MetaModifier) {
        mask |= Super;
    }
    if (modifiers & Qt::GroupSwitchModifier) {
        mask |= AltGr;
    }
    return mask;
}

bool AutoKeyBridge::isSynthetic(const KWin::InputDevice *device) const
{
    // Keys typed by org_kde_kwin_fake_input (AutoKey's own output) and other
    // virtual devices
    if (!device) {
        return true;
    }
    // For automated tests in a nested KWin, which only has synthetic input
    static const bool acceptSynthetic = qEnvironmentVariableIntValue("AUTOKEY_BRIDGE_ACCEPT_SYNTHETIC") == 1;
    if (acceptSynthetic) {
        return false;
    }
    const QString name = device->name();
    return name.contains(QLatin1String("Fake"), Qt::CaseInsensitive) || name.contains(QLatin1String("Virtual"), Qt::CaseInsensitive);
}

bool AutoKeyBridge::matchesHotkey(uint modifiers, uint keysym, const QString &windowClass, const QString &windowTitle) const
{
    for (const AutoKeyHotkey &hotkey : m_hotkeys) {
        if (hotkey.keysym != keysym || hotkey.modifiers != modifiers) {
            continue;
        }
        if (hotkey.filter.isEmpty()) {
            return true;
        }
        // Like AutoKey: Python's re.match() on the title or the class
        const auto anchored = QRegularExpression::AnchorAtOffsetMatchOption;
        if (hotkey.regex.match(windowTitle, 0, QRegularExpression::NormalMatch, anchored).hasMatch()
            || hotkey.regex.match(windowClass, 0, QRegularExpression::NormalMatch, anchored).hasMatch()) {
            return true;
        }
    }
    return false;
}

bool AutoKeyBridge::keyboardKey(KWin::KeyboardKeyEvent *event)
{
    // Our own modifier releases/presses (ClearModifiers/RestoreModifiers) and
    // synthetic input go straight through
    if (m_emitting || isSynthetic(event->device)) {
        return false;
    }

    const quint32 scanCode = event->nativeScanCode;
    if (isModifierScanCode(scanCode)) {
        if (event->state == KWin::KeyboardKeyState::Pressed) {
            m_heldModifiers[event->device].insert(scanCode);
        } else if (event->state == KWin::KeyboardKeyState::Released) {
            m_heldModifiers[event->device].remove(scanCode);
            if (m_clearedModifiers[event->device].remove(scanCode)) {
                // Already released for the clients by ClearModifiers()
                return true;
            }
        }
    }

    if (!m_monitors.isEmpty()) {
        sendToMonitors(QStringLiteral("MonitorKey"),
                       {scanCode, uint(event->nativeVirtualKey), event->state != KWin::KeyboardKeyState::Released,
                        event->state == KWin::KeyboardKeyState::Repeated, modifierMask(event->modifiers), event->text});
    }

    if (m_client.isEmpty()) {
        return false;
    }

    const bool pressed = event->state != KWin::KeyboardKeyState::Released;
    const bool repeat = event->state == KWin::KeyboardKeyState::Repeated;
    const uint keysym = levelKeysym(scanCode, 0);

    QString windowClass;
    QString windowTitle;
    if (KWin::Window *window = KWin::workspace()->activeWindow()) {
        // X11 style "instance.Class", which is what AutoKey window filters
        // were written against
        windowClass = window->resourceName() + QLatin1Char('.') + window->resourceClass();
        windowTitle = window->caption();
    }

    // The modifiers of the event include the key itself when it is a
    // modifier; hotkeys are only matched on non-modifier keys.
    const uint modifiers = modifierMask(event->modifiers);
    bool grabbed = false;
    if (pressed) {
        if (m_grabbedKeys.contains(scanCode)) {
            grabbed = true; // repeat of a grabbed key
        } else if (!repeat && matchesHotkey(modifiers, keysym, windowClass, windowTitle)) {
            m_grabbedKeys.insert(scanCode);
            grabbed = true;
        }
    } else {
        grabbed = m_grabbedKeys.remove(scanCode);
    }

    QDBusMessage signal = QDBusMessage::createTargetedSignal(m_client, s_path, s_interface, QStringLiteral("KeyEvent"));
    signal << keysym << levelKeysym(scanCode, 1) << levelKeysym(scanCode, 2) << scanCode << pressed << repeat << grabbed << modifiers
           << windowClass << windowTitle;
    QDBusConnection::sessionBus().send(signal);

    return grabbed;
}
