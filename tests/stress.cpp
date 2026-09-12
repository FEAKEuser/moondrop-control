// Hammers connect/disconnect to shake out crashes in the channel scan.
//
// Two regimes are exercised, because they fail differently:
//   * "churn": disconnect is called faster than a scan can finish.  This must not
//     crash, leak sockets/timers, or wedge the state machine - but it is expected
//     NOT to reach Connected, since every scan is interrupted on purpose.
//   * "settle": a connection is given time to complete at least once, which is
//     what proves the scan still works after all that churn.
// Only the second one may assert that a connection happened.
#include "moondropdevice.h"
#include "fakeheadset.h"
#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qputenv("MOONDROP_CONFIG", "/tmp/moondrop-stress.ini");
    QFile::remove("/tmp/moondrop-stress.ini");
    auto *fake = new Moondrop::FakeHeadset(Moondrop::FakeHeadset::Robin);
    fake->setChannel(16);
    auto *device = new Moondrop::MoondropDevice(nullptr, fake);
    device->disableStartupAutoConnect();
    device->setAddress(QStringLiteral("00:11:22:33:44:55"));
    device->setChannel(0);

    // Phase 1: churn - interrupt the scan constantly (motion is the point here,
    // not a completed connection).  Phase 2: settle - allow real connections.
    // Only phase 2 may assert that a connection happened: during churn every scan
    // is deliberately interrupted, so not connecting is the expected outcome.
    const int churnCycles = 60;
    const int settleCycles = 20;
    int cycle = 0;
    int connects = 0; // connections completed during the settle phase

    auto *timer = new QTimer(&app);
    timer->setInterval(120);
    QObject::connect(timer, &QTimer::timeout, [&] {
        ++cycle;
        const bool settling = cycle > churnCycles;
        if (settling) {
            // a scan needs ~1 s (silent channel + connect)
            timer->setInterval(1500);
        }

        if (device->state() == Moondrop::MoondropDevice::Connected) {
            if (settling) {
                ++connects;
            } else {
                device->disconnectDevice();
            }
        } else if (cycle <= churnCycles) {
            if (device->state() == Moondrop::MoondropDevice::Disconnected) {
                device->connectDevice();
            } else {
                device->disconnectDevice();
            }
        } else if (device->state() == Moondrop::MoondropDevice::Disconnected) {
            // settle phase: leave the scan alone so it can finish
            device->connectDevice();
        }

        // Checked last, so the Connected branch above cannot bypass it.
        if (cycle > churnCycles + settleCycles) {
            const bool ok = connects > 0;
            std::printf("%s  survived %d churn cycles + %d settle cycles, %d connection(s)\n",
                        ok ? "ok  " : "FAIL", churnCycles, settleCycles, connects);
            if (!ok) {
                std::printf("the scan never completed a connection even when given time "
                            "(channel scan is broken, not just interrupted)\n");
            }
            app.exit(ok ? 0 : 1);
        }
    });
    timer->start();
    QTimer::singleShot(60000, &app, [] { std::printf("FAIL: hang\n"); QCoreApplication::exit(1); });
    return app.exec();
}
