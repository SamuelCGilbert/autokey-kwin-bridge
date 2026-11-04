/*
    SPDX-FileCopyrightText: 2026 AutoKey KWin Bridge contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include <plugin.h>

#include "autokeybridge.h"

class KWIN_EXPORT AutoKeyBridgeFactory : public KWin::PluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginFactory_iid FILE "metadata.json")
    Q_INTERFACES(KWin::PluginFactory)

public:
    std::unique_ptr<KWin::Plugin> create() const override
    {
        return std::make_unique<AutoKeyBridge>();
    }
};

#include "main.moc"
