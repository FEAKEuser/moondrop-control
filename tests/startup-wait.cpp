// SPDX-License-Identifier: GPL-3.0-or-later
// "The applet started while my other headphone was already on."
//
// Regression: startChannelScan() parked in "waiting for the headphone" and
// returned without scheduling anything.  A headphone that was *already* connected
// when the applet started produces no BlueZ event, so the wait lasted until the
// user pressed "Retry now" - the reported symptom.  The wait must keep a retry
// running so the follow-the-live-headphone check runs again.
//
// Uses real BlueZ state; skipped when nothing suitable is connected.
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QFile>
#include <QTimer>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString offline = app.arguments().value(1);
    if (offline.isEmpty()) {
        std::printf("usage: moondrop-startup-wait-check <paired-but-offline-address>\n");
        std::printf("skipped  no offline address given\n");
        return 0;
    }

    qputenv("MOONDROP_CONFIG", "/tmp/moondrop-startup-wait.ini");
    QFile::remove("/tmp/moondrop-startup-wait.ini");

    auto *device = new Moondrop::MoondropDevice();
    device->disableStartupAutoConnect();
    device->setAddress(offline);
    device->setChannel(0);

    QObject::connect(device, &Moondrop::MoondropDevice::logMessage, [](const QString &m) {
        std::printf("  %s\n", qPrintable(m));
    });

    // If the run cannot get the headphone's single control channel (the live
    // applet may hold it), that is not a failure of this test - what matters is
    // that the *wait* was left, i.e. the address switched away from the offline
    // one and a connection was attempted.
    bool attempted = false;
    QObject::connect(device, &Moondrop::MoondropDevice::logMessage, [&](const QString &m) {
        if (m.contains(QStringLiteral("Looking for the control channel"))) {
            attempted = true;
        }
    });
    QTimer::singleShot(15000, &app, [&] {
        if (device->address() != offline || attempted) {
            std::printf("%s  left the wait for %s (now on %s, attempt made=%d)\n",
                        attempted ? "ok  " : "FAIL", qPrintable(offline),
                        qPrintable(device->address()), int(attempted));
            QCoreApplication::exit(attempted ? 0 : 1);
            return;
        }
        std::printf("FAIL  still on %s after 15 s - the wait never retried\n",
                    qPrintable(device->address()));
        QCoreApplication::exit(1);
    });

    QObject::connect(device, &Moondrop::MoondropDevice::stateChanged, [&] {
        if (device->state() != Moondrop::MoondropDevice::Connected) {
            return;
        }
        const bool switched = device->address() != offline;
        std::printf("%s  connected to %s (%s) without any BlueZ event or user input\n",
                    switched ? "ok  " : "FAIL", qPrintable(device->address()),
                    qPrintable(device->model()));
        QCoreApplication::exit(switched ? 0 : 1);
    });

    QTimer::singleShot(0, device, &Moondrop::MoondropDevice::connectDevice);
    return app.exec();
}
