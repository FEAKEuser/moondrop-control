// SPDX-License-Identifier: GPL-3.0-or-later
// Channel scan check: the automatic channel detection has to find the GAIA
// channel even when the first candidate accepts the connection and then stays
// silent (that is what a wrong RFCOMM channel looks like on a real headphone).
//
//   ./build/cli/moondrop-scan-check                 # fake headset (no hardware)
//   ./build/cli/moondrop-scan-check <bt-address>    # real headphone
//   ./build/cli/moondrop-scan-check --fresh         # real headphone, no address
//                                                   # configured (the applet has
//                                                   # to find it by itself)
#include "fakeheadset.h"
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

#include <cstdio>

static int failures = 0;

static void check(bool ok, const QString &what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString address = app.arguments().value(1);
    qputenv("MOONDROP_CONFIG", "/tmp/moondrop-scan-check.ini");
    QFile::remove("/tmp/moondrop-scan-check.ini");

    const bool freshRun = address == QLatin1String("--fresh");
    const bool fakeRun = address.isEmpty();
    Moondrop::MoondropDevice *device = nullptr;
    if (fakeRun) {
        // answers on channel 16 only: channel 1 accepts and stays silent, so the
        // scan has to move on instead of hanging on it
        auto *fake = new Moondrop::FakeHeadset(Moondrop::FakeHeadset::Edge);
        fake->setChannel(16);
        device = new Moondrop::MoondropDevice(nullptr, fake);
        device->setAddress(QStringLiteral("00:11:22:33:44:55"));
        device->setChannel(0); // auto detection
    } else if (freshRun) {
        // No address at all: exactly what a freshly added applet sees.  Do not
        // disable the startup path - finding the headphone is the point here.
        device = new Moondrop::MoondropDevice();
    } else {
        device = new Moondrop::MoondropDevice();
        device->setAddress(address);
        device->setChannel(0);
        device->disableStartupAutoConnect();
    }
    if (!freshRun) {
        device->disableStartupAutoConnect();
    }

    QElapsedTimer timer;
    timer.start();
    bool reported = false;
    const auto finish = [&] {
        if (reported) {
            return;
        }
        reported = true;
        std::printf("connected in %.0f ms\n", double(timer.elapsed()));
        std::printf(failures == 0 ? "\nall checks passed\n" : "\n%d check(s) failed\n", failures);
        QCoreApplication::exit(failures == 0 ? 0 : 1);
    };
    QObject::connect(device, &Moondrop::MoondropDevice::stateChanged, [&] {
        if (device->state() != Moondrop::MoondropDevice::Connected) {
            return;
        }
        check(device->connected(), QStringLiteral("the channel was found and the link is up"));
        check(device->liveChannel() > 0, QStringLiteral("a concrete channel was selected"));
        if (fakeRun) {
            check(device->liveChannel() == 16,
                  QStringLiteral("the answering channel (16) was chosen, not the silent one"));
        }
        // the model name arrives with the first state refresh, not with the
        // connection itself: wait for it instead of reading it too early
        QObject::connect(device, &Moondrop::MoondropDevice::infoChanged, [&] {
            if (device->model().length() > 0) {
                check(true, QStringLiteral("the model name was read (%1)").arg(device->model()));
                finish();
            }
        });
        QTimer::singleShot(8000, device, [&] {
            check(device->model().length() > 0, QStringLiteral("the model name was read"));
            finish();
        });
    });
    QObject::connect(device, &Moondrop::MoondropDevice::lastErrorChanged, [&] {
        if (!device->lastError().isEmpty()) {
            std::printf("error: %s\n", qPrintable(device->lastError()));
        }
    });

    if (!freshRun) {
        QTimer::singleShot(0, device, &Moondrop::MoondropDevice::connectDevice);
    }
    QTimer::singleShot(45000, &app, [&] {
        std::printf("FAIL  no connection within 45 s\n");
        QCoreApplication::exit(1);
    });
    return app.exec();
}
