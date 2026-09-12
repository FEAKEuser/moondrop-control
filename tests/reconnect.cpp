// SPDX-License-Identifier: GPL-3.0-or-later
// Automatic recovery when a headphone goes away: the backend must keep retrying
// on its own, so the user never has to press "Retry now" after switching from one
// pair to another.  The retry re-runs the channel scan, which follows whichever
// MOONDROP headphone is connected at that moment.
#include "fakeheadset.h"
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QFile>
#include <QTimer>

#include <cstdio>

static int failures = 0;

static void expect(bool ok, const QString &what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qputenv("MOONDROP_CONFIG", "/tmp/moondrop-reconnect.ini");
    QFile::remove("/tmp/moondrop-reconnect.ini");

    auto *fake = new Moondrop::FakeHeadset(Moondrop::FakeHeadset::Edge);
    fake->setChannel(1);
    auto *device = new Moondrop::MoondropDevice(nullptr, fake);
    device->disableStartupAutoConnect();
    device->setAddress(QStringLiteral("00:11:22:33:44:55"));
    device->setChannel(1);
    // default autoConnect/autoReconnect are on, which is the point of this test

    int phase = 0;
    QObject::connect(device, &Moondrop::MoondropDevice::infoChanged, [&] {
        if (device->model().isEmpty()) {
            return;
        }
        if (phase == 0) {
            phase = 1;
            expect(device->connected(), QStringLiteral("first connection established"));

            // The headphone is switched off: simulate the Bluetooth link going
            // down.  The backend must schedule a retry by itself.
            const QString before = device->address();
            Q_UNUSED(before)
            auto *transport = fake;
            transport->disconnectFromDevice();   // emits disconnected()
            QTimer::singleShot(50, &app, [&] {
                expect(!device->connected(), QStringLiteral("link is down after the device vanished"));
                expect(device->waitingForHeadphone() || device->state() != Moondrop::MoondropDevice::Connected,
                       QStringLiteral("the device is reported as gone"));
            });
        } else if (phase == 1) {
            // got here again without any user interaction
            phase = 2;
            expect(true, QStringLiteral("reconnected without pressing Retry (model %1)")
                             .arg(device->model()));
            std::printf(failures == 0 ? "\nall checks passed\n" : "\n%d check(s) failed\n", failures);
            QCoreApplication::exit(failures == 0 ? 0 : 1);
        }
    });

    QTimer::singleShot(25000, &app, [&] {
        std::printf("FAIL  did not reconnect on its own within 25 s "
                    "(the user would have to press Retry now)\n");
        QCoreApplication::exit(1);
    });

    QTimer::singleShot(0, device, &Moondrop::MoondropDevice::connectDevice);
    return app.exec();
}
