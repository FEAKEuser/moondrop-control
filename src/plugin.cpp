// SPDX-License-Identifier: GPL-3.0-or-later
#include "devicediscovery.h"
#include "moondropdevice.h"

#include <QQmlEngine>
#include <QQmlExtensionPlugin>

class MoondropPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)

public:
    void registerTypes(const char *uri) override
    {
        qmlRegisterType<Moondrop::DeviceDiscovery>(uri, 1, 0, "DeviceDiscovery");
        qmlRegisterSingletonType<Moondrop::MoondropDevice>(uri, 1, 0, "Moondrop",
                                                           [](QQmlEngine *, QJSEngine *) -> QObject * {
                                                               return new Moondrop::MoondropDevice();
                                                           });
    }
};

#include "plugin.moc"
