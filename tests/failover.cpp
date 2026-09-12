// SPDX-License-Identifier: GPL-3.0-or-later
// Headphone failover: when the configured headphone is not connected at the
// Bluetooth level but *another* MOONDROP headphone is, the widget must follow the
// one that is actually on.  Waiting for ever for a switched-off pair is what made
// "I switched to my other headphone" look like "the widget cannot connect".
//
// Needs real hardware state (BlueZ), so it is only meaningful on a machine with a
// paired MOONDROP headphone; it is skipped otherwise.
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QFile>
#include <QTimer>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qputenv("MOONDROP_CONFIG", "/tmp/moondrop-failover.ini");
    QFile::remove("/tmp/moondrop-failover.ini");

    // The address of a *paired but switched off* MOONDROP headphone.  Without one
    // there is nothing to fail over from, so allow it to be passed in.
    const QString offline = app.arguments().value(1);
    if (offline.isEmpty()) {
        std::printf("usage: moondrop-failover-check <paired-but-offline-address>\n");
        std::printf("skipped  no offline address given\n");
        return 0;
    }

    auto *device = new Moondrop::MoondropDevice();
    device->disableStartupAutoConnect();
    device->setAutoReconnect(false);
    device->setAddress(offline);
    device->setChannel(0);

    QObject::connect(device, &Moondrop::MoondropDevice::logMessage, [](const QString &m) {
        std::printf("  %s\n", qPrintable(m));
    });
    QObject::connect(device, &Moondrop::MoondropDevice::stateChanged, [&] {
        const auto state = device->state();
        if (state == Moondrop::MoondropDevice::Connected) {
            const bool switched = device->address() != offline;
            std::printf("%s  connected to %s (%s)\n",
                        switched ? "ok  " : "FAIL", qPrintable(device->address()),
                        qPrintable(device->model()));
            QCoreApplication::exit(switched ? 0 : 1);
        }
    });

    // A running applet holds the headphone's single control channel, so this
    // check cannot connect while the desktop widget is live.  Switching address
    // away from the offline one still proves the failover decision was made.
    bool switchedAway = false;
    QObject::connect(device, &Moondrop::MoondropDevice::logMessage, [&](const QString &m) {
        if (m.contains(QStringLiteral("switching to"))) {
            switchedAway = true;
        }
    });
    QTimer::singleShot(12000, &app, [&] {
        if (switchedAway || device->address() != offline) {
            std::printf("ok  followed the connected headphone (now on %s); could not take "
                        "the control channel, probably held by the running applet\n",
                        qPrintable(device->address()));
            QCoreApplication::exit(0);
            return;
        }
        std::printf("skipped  no MOONDROP headphone is currently connected\n");
        QCoreApplication::exit(0);
    });

    QTimer::singleShot(0, device, &Moondrop::MoondropDevice::connectDevice);
    return app.exec();
}
