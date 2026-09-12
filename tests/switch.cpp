// SPDX-License-Identifier: GPL-3.0-or-later
// Switching headphones: after connecting device A, disconnecting and connecting
// device B, every piece of device information must describe B - never A.
//
// Regression: m_model/m_firmware/m_serial/m_features/m_profile were only ever
// assigned, never cleared, so the widget kept showing the previous headphone.
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
    qputenv("MOONDROP_CONFIG", "/tmp/moondrop-switch.ini");
    QFile::remove("/tmp/moondrop-switch.ini");

    // One fake transport whose model we replace, exactly like plugging in another
    // headphone on the same widget instance.
    auto *fake = new Moondrop::FakeHeadset(Moondrop::FakeHeadset::Robin);
    fake->setChannel(1);
    auto *device = new Moondrop::MoondropDevice(nullptr, fake);
    device->disableStartupAutoConnect();
    device->setAutoReconnect(false);   // keep each phase stable
    device->setAddress(QStringLiteral("00:11:22:33:44:55"));
    device->setChannel(1);

    int phase = 0;

    const auto startSecondDevice = [&] {
        // Swap in a different model while the device is disconnected.
        auto *second = new Moondrop::FakeHeadset(Moondrop::FakeHeadset::Edge);
        second->setChannel(1);
        device->disconnectDevice();
        // Re-point the device at the new headphone by connecting with a fresh
        // transport: MoondropDevice has no setter, so use the scan path with the
        // same address after clearing the old info (see below).
        Q_UNUSED(second)
    };
    Q_UNUSED(startSecondDevice)

    // The model arrives with the first state refresh, not with the connection:
    // wait for it (infoChanged) instead of reading it too early.
    QObject::connect(device, &Moondrop::MoondropDevice::infoChanged, [&] {
        if (phase != 0 || device->model().isEmpty()) {
            return;
        }
        expect(device->model() == QLatin1String("MOONDROP Robin"),
               QStringLiteral("first headphone reports its own model (%1)").arg(device->model()));
        expect(device->profileId() == QLatin1String("robin"),
               QStringLiteral("first headphone uses its own profile (%1)").arg(device->profileId()));
        phase = 1;

        // The user switches headphones: disconnect, then reconnect.  Nothing of
        // the first headphone may survive into the disconnected state.
        device->disconnectDevice();
        // the user swaps the headphone on the desk
        static_cast<Moondrop::FakeHeadset *>(fake)->setModel(Moondrop::FakeHeadset::Edge);
        QTimer::singleShot(200, &app, [&] {
            expect(device->model().isEmpty(),
                   QStringLiteral("model is cleared while nothing is connected (was '%1')")
                       .arg(device->model()));
            expect(device->firmwareVersion().isEmpty(),
                   QStringLiteral("firmware is cleared while disconnected (was '%1')")
                       .arg(device->firmwareVersion()));
            expect(device->profileId() == QLatin1String("unknown")
                       || device->profileId() == QLatin1String("generic"),
                   QStringLiteral("profile falls back while disconnected (%1)").arg(device->profileId()));
            expect(device->features().isEmpty(),
                   QStringLiteral("capability list is cleared while disconnected (%1 entries)")
                       .arg(device->features().size()));

            // Now connect the *other* headphone: it must describe itself, not the
            // one we were talking to before.
            QObject::connect(device, &Moondrop::MoondropDevice::infoChanged, [&] {
                if (phase != 1 || device->model().isEmpty()) {
                    return;
                }
                phase = 2;
                expect(device->model() == QLatin1String("MOONDROP EDGE"),
                       QStringLiteral("the second headphone reports its own model (%1)")
                           .arg(device->model()));
                expect(device->model() != QLatin1String("MOONDROP Robin"),
                       QStringLiteral("the previous model did not leak through"));
                expect(device->profileId() == QLatin1String("edge"),
                       QStringLiteral("the second headphone uses its own profile (%1)")
                           .arg(device->profileId()));
                expect(!device->firmwareVersion().isEmpty(),
                       QStringLiteral("the second headphone's firmware was read (%1)")
                           .arg(device->firmwareVersion()));
                std::printf(failures == 0 ? "\nall checks passed\n" : "\n%d check(s) failed\n", failures);
                QCoreApplication::exit(failures == 0 ? 0 : 1);
            });
            device->connectDevice();
        });
    });

    QTimer::singleShot(0, device, &Moondrop::MoondropDevice::connectDevice);
    QTimer::singleShot(20000, &app, [] {
        std::printf("FAIL  no connection within 20 s\n");
        QCoreApplication::exit(1);
    });
    return app.exec();
}
