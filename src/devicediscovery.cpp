// SPDX-License-Identifier: GPL-3.0-or-later
#include "devicediscovery.h"

#include "i18n.h"

#include <QDBusArgument>
#include <QTimer>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDebug>

#include <algorithm>

namespace Moondrop {

DeviceDiscovery::DeviceDiscovery(QObject *parent)
    : QObject(parent)
{
}

void DeviceDiscovery::refresh()
{
    m_busy = true;
    Q_EMIT busyChanged();

    const QDBusConnection bus = QDBusConnection::systemBus();

    // 1) ObjectManager gives us every object BlueZ knows about; we only need the
    //    device object paths.  The values of that map are awkward to demarshal
    //    (a{sa{sv}} inside a variant), so the properties are fetched separately.
    const QDBusMessage reply =
        bus.call(QDBusMessage::createMethodCall(QStringLiteral("org.bluez"),
                                                QStringLiteral("/"),
                                                QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                                QStringLiteral("GetManagedObjects")));
    if (reply.type() != QDBusMessage::ReplyMessage) {
        m_error = reply.errorMessage().isEmpty()
                      ? moondropTr("BlueZ is not reachable on the system bus.")
                      : reply.errorMessage();
        m_busy = false;
        Q_EMIT errorChanged();
        Q_EMIT busyChanged();
        return;
    }

    QStringList devicePaths;
    const QDBusArgument objects = reply.arguments().at(0).value<QDBusArgument>();
    objects.beginMap();
    while (!objects.atEnd()) {
        objects.beginMapEntry();
        QString path;
        QVariant ignored;
        objects >> path >> ignored;
        objects.endMapEntry();
        // device objects look like /org/bluez/hci0/dev_41_42_95_60_16_09 (four
        // slashes), their children (fd0, sep1, ...) have one element more
        if (path.contains(QLatin1String("/dev_")) && path.count(QLatin1Char('/')) == 4) {
            devicePaths.append(path);
        }
    }
    objects.endMap();

    // 2) org.freedesktop.DBus.Properties.GetAll returns a plain a{sv} map that
    //    Qt demarshals into a QVariantMap with usable types.
    QVariantList devices;
    for (const QString &path : devicePaths) {
        QDBusInterface properties(QStringLiteral("org.bluez"), path,
                                 QStringLiteral("org.freedesktop.DBus.Properties"), bus);
        const QDBusMessage deviceReply = properties.call(QStringLiteral("GetAll"),
                                                         QStringLiteral("org.bluez.Device1"));
        if (deviceReply.type() != QDBusMessage::ReplyMessage) {
            continue;
        }
        const QVariantMap props = qdbus_cast<QVariantMap>(deviceReply.arguments().at(0));

        QString name = props.value(QStringLiteral("Alias")).toString();
        if (name.isEmpty()) {
            name = props.value(QStringLiteral("Name")).toString();
        }
        const QString address = props.value(QStringLiteral("Address")).toString();
        if (name.isEmpty()) {
            name = address;
        }

        QVariantMap entry;
        entry.insert(QStringLiteral("address"), address);
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("path"), path);
        entry.insert(QStringLiteral("paired"), props.value(QStringLiteral("Paired")).toBool());
        entry.insert(QStringLiteral("bonded"), props.value(QStringLiteral("Bonded")).toBool());
        entry.insert(QStringLiteral("connected"), props.value(QStringLiteral("Connected")).toBool());
        entry.insert(QStringLiteral("trusted"), props.value(QStringLiteral("Trusted")).toBool());
        entry.insert(QStringLiteral("legacyPairing"), props.value(QStringLiteral("LegacyPairing")).toBool());

        QString icon = props.value(QStringLiteral("Icon")).toString();
        if (icon.isEmpty() && name.contains(QLatin1String("MOONDROP"), Qt::CaseInsensitive)) {
            icon = QStringLiteral("audio-headphones");
        }
        entry.insert(QStringLiteral("icon"), icon);

        const QDBusMessage batteryReply =
            properties.call(QStringLiteral("Get"), QStringLiteral("org.bluez.Battery1"),
                            QStringLiteral("Percentage"));
        if (batteryReply.type() == QDBusMessage::ReplyMessage) {
            entry.insert(QStringLiteral("bluetoothBattery"),
                         batteryReply.arguments().at(0).value<QDBusVariant>().variant().toInt());
        }
        devices.append(entry);
    }

    // MOONDROP devices first, then connected ones, then paired ones, then by name
    std::sort(devices.begin(), devices.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap left = a.toMap();
        const QVariantMap right = b.toMap();
        const auto isMoondrop = [](const QVariantMap &entry) {
            return entry.value(QStringLiteral("name")).toString().contains(QLatin1String("MOONDROP"),
                                                                           Qt::CaseInsensitive);
        };
        if (isMoondrop(left) != isMoondrop(right)) {
            return isMoondrop(left);
        }
        const bool leftConnected = left.value(QStringLiteral("connected")).toBool();
        const bool rightConnected = right.value(QStringLiteral("connected")).toBool();
        if (leftConnected != rightConnected) {
            return leftConnected;
        }
        const bool leftPaired = left.value(QStringLiteral("paired")).toBool();
        const bool rightPaired = right.value(QStringLiteral("paired")).toBool();
        if (leftPaired != rightPaired) {
            return leftPaired;
        }
        return left.value(QStringLiteral("name")).toString() < right.value(QStringLiteral("name")).toString();
    });

    m_devices = devices;
    m_error.clear();
    m_busy = false;
    Q_EMIT devicesChanged();
    Q_EMIT errorChanged();
    Q_EMIT busyChanged();
}

// ---------------------------------------------------------------------------
// BlueZWatcher
// ---------------------------------------------------------------------------

namespace {

// "AA:BB:CC:DD:EE:FF" -> "dev_AA_BB_CC_DD_EE_FF"
QString addressToPathComponent(const QString &address)
{
    return QStringLiteral("dev_") + QString(address).replace(QLatin1Char(':'), QLatin1Char('_'));
}

// Boolean out of a PropertiesChanged dictionary.
//
// Qt demarshals `a{sv}` with the inner variant already unwrapped, so the value is
// usually a plain bool.  Wrapping that in qdbus_cast<QVariant>() again yields an
// *invalid* QVariant whose toBool() is false - which silently turned every
// "Connected = true" signal into a disconnect, leaving the applet convinced the
// headphone was gone until it was restarted.  Returning the raw bool keeps both
// shapes working, and a nested QDBusVariant (what a QDBusArgument delivers) is
// unwrapped explicitly.
bool variantToBool(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant().toBool();
    }
    return value.toBool();
}

} // namespace

BlueZWatcher::BlueZWatcher(QObject *parent)
    : QObject(parent)
{
}

void BlueZWatcher::watchAllHeadsets(bool enabled)
{
    if (m_watchAll == enabled) {
        return;
    }
    m_watchAll = enabled;
    refreshAllHeadsets();
}

void BlueZWatcher::refreshAllHeadsets()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    for (const QString &path : std::as_const(m_allPaths)) {
        bus.disconnect(QStringLiteral("org.bluez"), path,
                       QStringLiteral("org.freedesktop.DBus.Properties"),
                       QStringLiteral("PropertiesChanged"), this,
                       SLOT(onAnyPropertiesChanged(QString, QVariantMap, QStringList)));
    }
    m_allPaths.clear();
    if (!m_watchAll) {
        return;
    }

    // Qt's QDBusConnection::connect() matches the object path exactly - a "/"
    // subscription does NOT receive signals from deeper paths (verified against
    // BlueZ 5.87: the signal arrives on dbus-monitor but never reaches the slot).
    // BlueZ also has no stable "all devices" object, so the paths are enumerated
    // and each is subscribed individually.  Devices are few, and a new one is
    // picked up by refreshAllHeadsets() when the list changes.
    QDBusMessage request =
        QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), QStringLiteral("/"),
                                       QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                       QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = bus.call(request, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return;
    }
    const QDBusArgument objects = reply.arguments().at(0).value<QDBusArgument>();
    objects.beginMap();
    while (!objects.atEnd()) {
        objects.beginMapEntry();
        QString objectPath;
        QVariant ignored;
        objects >> objectPath >> ignored;
        objects.endMapEntry();
        if (!objectPath.contains(QLatin1String("/dev_")) || objectPath.count(QLatin1Char('/')) != 4) {
            continue;
        }
        m_allPaths.append(objectPath);
        // the 4-argument slot form (no explicit signature, no path argument); this
        // is the form Qt matches successfully - a signature string breaks it
        bus.connect(QStringLiteral("org.bluez"), objectPath,
                    QStringLiteral("org.freedesktop.DBus.Properties"),
                    QStringLiteral("PropertiesChanged"), this,
                    SLOT(onAnyPropertiesChanged(QString, QVariantMap, QStringList)));
    }
    objects.endMap();
}

QString BlueZWatcher::pathToAddress(const QString &path)
{
    // /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF -> AA:BB:CC:DD:EE:FF
    const int marker = path.indexOf(QLatin1String("/dev_"));
    if (marker < 0) {
        return QString();
    }
    QString address = path.mid(marker + 5);
    address.replace(QLatin1Char('_'), QLatin1Char(':'));
    return address;
}

void BlueZWatcher::onAnyPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                          const QStringList &invalidated)
{
    Q_UNUSED(invalidated)
    if (interface != QLatin1String("org.bluez.Device1")) {
        return;
    }
    // The emitting object path is only available through the D-Bus message:
    // sender() is null for system-bus signals delivered to a QDBusContext slot
    // (verified against BlueZ 5.87), so message().path() is the reliable source.
    if (!calledFromDBus()) {
        return;
    }
    const QString address = pathToAddress(message().path());
    if (address.isEmpty()) {
        return;
    }
    if (changed.contains(QStringLiteral("Connected"))
        && !variantToBool(changed.value(QStringLiteral("Connected")))) {
        return; // only appearing transitions matter here
    }
    Q_EMIT headphoneAppeared(address);
}

void BlueZWatcher::watch(const QString &address)
{
    if (m_address == address) {
        return;
    }
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!m_path.isEmpty()) {
        bus.disconnect(QStringLiteral("org.bluez"), m_path, QStringLiteral("org.freedesktop.DBus.Properties"),
                       QStringLiteral("PropertiesChanged"), this,
                       SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    }
    m_address = address;
    m_path.clear();
    m_connected = false;
    if (m_address.isEmpty()) {
        return;
    }
    resolvePath();
}

void BlueZWatcher::resolvePath()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    const QDBusMessage reply =
        bus.call(QDBusMessage::createMethodCall(QStringLiteral("org.bluez"),
                                                QStringLiteral("/"),
                                                QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                                QStringLiteral("GetManagedObjects")));
    if (reply.type() != QDBusMessage::ReplyMessage) {
        return;
    }

    const QString wanted = addressToPathComponent(m_address);
    QString path;
    const QDBusArgument objects = reply.arguments().at(0).value<QDBusArgument>();
    objects.beginMap();
    while (!objects.atEnd()) {
        objects.beginMapEntry();
        QString objectPath;
        QVariant ignored;
        objects >> objectPath >> ignored;
        objects.endMapEntry();
        if (objectPath.endsWith(wanted)) {
            path = objectPath;
            break;
        }
    }
    objects.endMap();

    if (path.isEmpty()) {
        return;
    }
    m_path = path;

    // read the current state, so we do not wait for the next change
    QDBusInterface properties(QStringLiteral("org.bluez"), path,
                              QStringLiteral("org.freedesktop.DBus.Properties"), bus);
    const QDBusMessage connectedReply =
        properties.call(QStringLiteral("Get"), QStringLiteral("org.bluez.Device1"),
                        QStringLiteral("Connected"));
    if (connectedReply.type() == QDBusMessage::ReplyMessage) {
        m_connected = connectedReply.arguments().at(0).value<QDBusVariant>().variant().toBool();
        if (m_connected) {
            Q_EMIT deviceConnected(m_address);
        }
    }

    bus.connect(QStringLiteral("org.bluez"), path, QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
}

void BlueZWatcher::onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                       const QStringList &invalidated)
{
    Q_UNUSED(invalidated)
    if (interface != QLatin1String("org.bluez.Device1")) {
        return;
    }
    const auto it = changed.constFind(QStringLiteral("Connected"));
    if (it == changed.constEnd()) {
        return;
    }
    const bool connected = variantToBool(it.value());
    if (connected == m_connected) {
        return;
    }
    m_connected = connected;
    if (connected) {
        Q_EMIT deviceConnected(m_address);
    } else {
        Q_EMIT deviceDisconnected(m_address);
        // The device object may be removed while it is switched off (BlueZ drops
        // it for some adapters); re-resolving on the way back keeps the
        // subscription valid so the "it is back" event is actually delivered.
        QTimer::singleShot(2500, this, [this] {
            if (!m_address.isEmpty() && !m_connected) {
                resolvePath();
            }
        });
    }
}

} // namespace Moondrop
