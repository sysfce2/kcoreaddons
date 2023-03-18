// SPDX-FileCopyrightText: 2021 Alexander Lohnau <alexander.lohnau@gmx.de>
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "kpluginfactory.h"

class StaticSimplePluginClass : public QObject
{
    Q_OBJECT

public:
    explicit StaticSimplePluginClass(QObject *)
    {
    }
};

K_PLUGIN_CLASS_WITH_JSON(StaticSimplePluginClass, "data/jsonplugin.json")

#include "statickpluginclass.moc"
