// SPDX-License-Identifier: GPL-3.0-or-later
// The EBUSY path: when another program holds the headphone's single control
// connection, the scan must give up after its retry budget and report that,
// instead of retrying the same channel for ever (which left the applet spinning
// in "Connecting" with no error).
#include "fakeheadset.h"
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QFile>
#include <QTimer>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qputenv("MOONDROP_CONFIG", "/tmp/moondrop-ebusy.ini");
    QFile::remove("/tmp/moondrop-ebusy.ini");

    auto *fake = new Moondrop::FakeHeadset(Moondrop::FakeHeadset::Edge);
    fake->setChannel(1);
    fake->setBusy(true); // every connect fails with EBUSY

    auto *device = new Moondrop::MoondropDevice(nullptr, fake);
    device->disableStartupAutoConnect();
    // no automatic retry, so the give-up state is stable and observable
    device->setAutoReconnect(false);
    device->setAddress(QStringLiteral("00:11:22:33:44:55"));
    device->setChannel(1);

    // Count how often the scan actually tried to connect: the retry budget has to
    // bound it. Without the budget this grows without limit.
    int attempts = 0;
    QObject::connect(fake, &Moondrop::Transport::errorOccurred, [&] { ++attempts; });

    QTimer::singleShot(25000, &app, [&] {
        std::printf("FAIL  still trying after 25 s (%d refused attempt(s)) - "
                    "the EBUSY retry loop never gave up\n", attempts);
        QCoreApplication::exit(1);
    });

    // The error is what identifies the give-up state; watching stateChanged alone
    // would fire on the intermediate Disconnected of the first attempt.
    QObject::connect(device, &Moondrop::MoondropDevice::lastErrorChanged, [&] {
        if (device->lastError().isEmpty()) {
            return;
        }
        const bool mentionsConflict = device->lastError().contains(QStringLiteral("in use"));
        // The budget is 4 retries on one candidate, so a single-candidate scan
        // must stop in a small, bounded number of attempts.
        const bool bounded = attempts > 0 && attempts <= 8;
        const bool ok = mentionsConflict && !device->busy() && bounded;
        std::printf("%s  gave up after %d refused attempt(s), busy=%d\n",
                    ok ? "ok  " : "FAIL", attempts, int(device->busy()));
        std::printf("      error: %s\n", qPrintable(device->lastError()));
        QCoreApplication::exit(ok ? 0 : 1);
    });

    QTimer::singleShot(0, device, &Moondrop::MoondropDevice::connectDevice);
    return app.exec();
}
