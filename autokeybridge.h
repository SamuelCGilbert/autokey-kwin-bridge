/*
    SPDX-FileCopyrightText: 2026 AutoKey KWin Bridge contributors
    SPDX-License-Identifier: GPL-3.0-or-later

    AutoKey needs to see what is typed (abbreviations) and to grab its hotkeys
    so they don't reach the focused application. On X11 it used XRecord and
    XGrabKey; on Wayland there is no such API for clients.

    This KWin plugin gives AutoKey the same from inside the compositor:
    - every key press/release is sent to the one registered AutoKey process
      (unicast D-Bus signal), with the active window's class and title;
    - AutoKey registers its hotkeys (modifiers, key, window filter regex) and
      matching key presses are consumed before they reach applications or
      KDE's global shortcuts.
    Synthetic input (org_kde_kwin_fake_input, which AutoKey uses to type) is
    ignored, so AutoKey doesn't see its own output.

    D-Bus: service org.autokey.KWinBridge, object /AutoKeyBridge,
    interface org.autokey.KWinBridge
      Register()                       -> the caller receives KeyEvent signals
      SetHotkeys(a(uus) hotkeys)       -> (modifier mask, level 0 keysym, filter regex)
      signal KeyEvent(u keysym, u shiftedKeysym, u altGrKeysym, u scanCode,
                      b pressed, b repeat, b grabbed, u modifiers,
                      s windowClass, s windowTitle)
    Modifier mask: 1 Shift, 2 Control, 4 Alt, 8 Super, 16 AltGr
*/
#pragma once

#include "input.h"
#include "input_event.h"
#include "plugin.h"

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

struct AutoKeyHotkey
{
    uint modifiers = 0;
    uint keysym = 0;
    QString filter;
    QRegularExpression regex;
};

class AutoKeyBridge : public KWin::Plugin, public KWin::InputEventFilter, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.autokey.KWinBridge")

public:
    AutoKeyBridge();
    ~AutoKeyBridge() override;

    bool keyboardKey(KWin::KeyboardKeyEvent *event) override;

public Q_SLOTS:
    void Register();
    void SetHotkeys(const QDBusMessage &message);
    QString Version() const;

private:
    void clientGone();
    uint levelKeysym(quint32 scanCode, int level) const;
    uint modifierMask(Qt::KeyboardModifiers modifiers) const;
    bool isSynthetic(const KWin::KeyboardKeyEvent *event) const;
    bool matchesHotkey(uint modifiers, uint keysym, const QString &windowClass, const QString &windowTitle) const;

    QString m_client;
    QDBusServiceWatcher *m_watcher = nullptr;
    QList<AutoKeyHotkey> m_hotkeys;
    QSet<quint32> m_grabbedKeys;
};
