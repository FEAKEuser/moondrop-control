// SPDX-License-Identifier: GPL-3.0-or-later
// Lists Bluetooth devices known to BlueZ so the widget can offer a picker.
#pragma once

#include <QDBusContext>
#include <QObject>
#include <QStringList>
#include <QVariantList>

namespace Moondrop {

class DeviceDiscovery : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit DeviceDiscovery(QObject *parent = nullptr);

    QVariantList devices() const { return m_devices; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void devicesChanged();
    void busyChanged();
    void errorChanged();

private:
    QVariantList m_devices;
    bool m_busy = false;
    QString m_error;
};

// Watches one headphone on the system bus and reports when it connects or
// disconnects at the Bluetooth level.  The applet uses this to connect as soon
// as the headphones are switched on, instead of polling.
class BlueZWatcher : public QObject, protected QDBusContext
{
    Q_OBJECT
public:
    explicit BlueZWatcher(QObject *parent = nullptr);

    // Watch this address (empty string stops watching).
    void watch(const QString &address);
    // Also report *any* MOONDROP headphone that connects, not just the watched
    // one.  Requires the ObjectManager interface on org.bluez.
    void watchAllHeadsets(bool enabled);
    QString address() const { return m_address; }
    // whether the watched device is connected at the Bluetooth level right now
    bool isDeviceConnected() const { return m_connected; }
    // the BlueZ object path, empty while the device is not known to BlueZ (or not
    // resolved yet); lets callers distinguish "offline" from "unknown"
    QString path() const { return m_path; }

Q_SIGNALS:
    void deviceConnected(const QString &address);
    void deviceDisconnected(const QString &address);
    // Any MOONDROP headphone that BlueZ reports as connected, whichever one is
    // being watched.  Lets the backend react to the user switching pairs instead
    // of waiting for the next reconnect tick.
    void headphoneAppeared(const QString &address);

private Q_SLOTS:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                             const QStringList &invalidated);
    // notification from one of the per-device subscriptions
    void onAnyPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                const QStringList &invalidated);

private:
    // finds the BlueZ object path of the watched device (hci index varies)
    void resolvePath();
    // (re)subscribe to every known device path for headphoneAppeared()
    void refreshAllHeadsets();
    static QString pathToAddress(const QString &path);

    QString m_address;
    QString m_path;
    bool m_connected = false;
    bool m_watchAll = false;
    QStringList m_allPaths;
};

} // namespace Moondrop
