// SPDX-License-Identifier: GPL-3.0-or-later
// "The widget stops picking the headphone up after a while."
//
// The watcher is what tells the backend that the headphone came back, and it
// learns that from a BlueZ PropertiesChanged signal.  The payload Qt delivers
// for `a{sv}` has the inner variant already unwrapped: `changed["Connected"]` is
// a *plain bool* QVariant.  Wrapping it in qdbus_cast<QVariant>() again returns
// an invalid QVariant, whose toBool() is false - so every "Connected = true"
// signal was read as false, m_connected stayed stuck at false, and the backend
// was never told the headphone had returned.  The applet then sat on
// "waiting for the headphone" until it was restarted.
//
// This pins the parsing down without hardware: the values BlueZ actually sends
// are fed to the same slot the bus calls, and the result is checked.
//
// Two shapes have to work, because which one arrives depends on how the signal
// was demarshalled (a plain bool, or a QDBusVariant when it came through a
// QDBusArgument).
#include "devicediscovery.h"

#include <QCoreApplication>
#include <QDBusVariant>
#include <QMetaObject>
#include <QStringList>
#include <QVariantMap>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) {
        ++failures;
    }
}

// The slot is private, which is deliberate: it is a bus callback, not API.  It is
// still reached through the meta object, which is exactly how Qt calls it.
bool feedConnected(Moondrop::BlueZWatcher &watcher, const QVariant &connectedValue)
{
    QVariantMap changed;
    changed.insert(QStringLiteral("Connected"), connectedValue);
    QStringList invalidated;
    return QMetaObject::invokeMethod(&watcher, "onPropertiesChanged", Qt::DirectConnection,
                                     Q_ARG(QString, QStringLiteral("org.bluez.Device1")),
                                     Q_ARG(QVariantMap, changed),
                                     Q_ARG(QStringList, invalidated));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    Moondrop::BlueZWatcher watcher;
    // watch() needs BlueZ to resolve a path; the slot is exercised directly so the
    // check also runs on a machine without the headphone.  What matters is the
    // address the watcher was asked about.
    QObject::connect(&watcher, &Moondrop::BlueZWatcher::deviceConnected,
                     [](const QString &address) {
                         std::printf("  deviceConnected(%s)\n", qPrintable(address));
                     });
    QObject::connect(&watcher, &Moondrop::BlueZWatcher::deviceDisconnected,
                     [](const QString &address) {
                         std::printf("  deviceDisconnected(%s)\n", qPrintable(address));
                     });

    int connectedSignals = 0;
    int disconnectedSignals = 0;
    QObject::connect(&watcher, &Moondrop::BlueZWatcher::deviceConnected,
                     [&](const QString &) { ++connectedSignals; });
    QObject::connect(&watcher, &Moondrop::BlueZWatcher::deviceDisconnected,
                     [&](const QString &) { ++disconnectedSignals; });

    // 1) A plain bool, which is what arrives for an `a{sv}` signal.  This is the
    //    shape that used to be misread.
    check(feedConnected(watcher, QVariant(true)), "the slot is reachable through the meta object");
    check(connectedSignals == 1, "Connected=true (plain bool) reports the headphone as connected");
    check(watcher.isDeviceConnected(), "Connected=true (plain bool) leaves the state connected");

    // 2) A disconnect must be reported too, and must flip the state back.
    feedConnected(watcher, QVariant(false));
    check(disconnectedSignals == 1, "Connected=false reports the headphone as gone");
    check(!watcher.isDeviceConnected(), "Connected=false leaves the state disconnected");

    // 3) The same value nested in a QDBusVariant, as a QDBusArgument delivers it.
    check(feedConnected(watcher, QVariant::fromValue(QDBusVariant(true))),
          "the slot accepts a nested QDBusVariant");
    check(connectedSignals == 2, "Connected=true (QDBusVariant) reports the headphone as connected");
    check(watcher.isDeviceConnected(), "Connected=true (QDBusVariant) leaves the state connected");

    // 4) A repeat of the same value is not a transition and must not be re-emitted.
    feedConnected(watcher, QVariant(true));
    check(connectedSignals == 2, "a repeated Connected=true is not reported twice");

    // 5) A signal that does not carry Connected must be ignored.
    {
        QVariantMap changed;
        changed.insert(QStringLiteral("ServicesResolved"), QVariant(true));
        QStringList invalidated;
        QMetaObject::invokeMethod(&watcher, "onPropertiesChanged", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("org.bluez.Device1")),
                                  Q_ARG(QVariantMap, changed),
                                  Q_ARG(QStringList, invalidated));
    }
    check(connectedSignals == 2, "a signal without Connected does not report a connection");

    // 6) Another interface on the same path must be ignored.
    {
        QVariantMap changed;
        changed.insert(QStringLiteral("Connected"), QVariant(true));
        QStringList invalidated;
        QMetaObject::invokeMethod(&watcher, "onPropertiesChanged", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("org.bluez.MediaControl1")),
                                  Q_ARG(QVariantMap, changed),
                                  Q_ARG(QStringList, invalidated));
    }
    check(connectedSignals == 2, "another interface's Connected does not report a connection");

    std::printf("%s  %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
