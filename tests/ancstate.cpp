// SPDX-License-Identifier: GPL-3.0-or-later
// Noise cancelling state across connections, run against the fake headset:
//
//   ./build/cli/moondrop-ancstate-check
//
// Two behaviours are covered:
//
//   * with "restore the last mode" on, the mode last set from the widget is
//     written back after every connection (the fake starts with ANC off, like a
//     headphone that does not keep the setting itself);
//   * with it off, the mode the headphone reports is what the widget shows, and
//     the remembered value is only kept for the settings page.
#include "fakeheadset.h"
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTimer>

#include <cstdio>

using namespace Moondrop;

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition) {
        std::printf("ok   %s\n", qPrintable(what));
    } else {
        std::printf("FAIL %s\n", qPrintable(what));
        ++failures;
    }
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
}

// counts the frames of the restore path, so the test can prove that connecting
// writes the mode once and idle time writes nothing
void watchRestoreFrames(MoondropDevice &device, int *count)
{
    QObject::connect(&device, &MoondropDevice::logMessage, [count](const QString &message) {
        if (message.startsWith(QLatin1String("TX ")) && message.contains(QLatin1String("(anc-restore)"))) {
            ++(*count);
        }
    });
}

const QString address = QStringLiteral("00:11:22:33:44:55");

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // never touch the user's configuration
    const QString configPath = QDir::temp().filePath(QStringLiteral("moondrop-ancstate.ini"));
    QFile::remove(configPath);
    qputenv("MOONDROP_CONFIG", configPath.toLocal8Bit());

    // --- 1. remember what the user sets, across sessions ---------------------
    {
        MoondropDevice device(nullptr, new FakeHeadset(FakeHeadset::Edge));
        device.disableStartupAutoConnect();
        device.setAddress(address);
        device.setChannel(1);

        check(device.ancLastMode() == -1, QStringLiteral("nothing is remembered for a fresh configuration"));

        device.connectDevice();
        check(waitFor([&] { return device.connected(); }), QStringLiteral("connects to the fake headset"));
        check(waitFor([&] { return !device.busy(); }), QStringLiteral("initial refresh finishes"));
        check(device.ancMode() == AncOff, QStringLiteral("the mode reported by the headphone is shown"));

        device.setAncMode(AncNoiseCancelling);
        check(waitFor([&] { return !device.busy(); }, 6000), QStringLiteral("ANC write completes"));
        check(device.ancMode() == AncNoiseCancelling, QStringLiteral("the mode is applied"));
        check(device.ancLastMode() == AncNoiseCancelling,
              QStringLiteral("the applied mode is remembered for this headphone"));
        device.disconnectDevice();

        device.setAddress(QStringLiteral("00:11:22:33:44:66"));
        check(device.ancLastMode() == -1,
              QStringLiteral("another headphone has its own remembered mode"));
        device.setAddress(address);
        check(device.ancLastMode() == AncNoiseCancelling,
              QStringLiteral("switching back shows the remembered mode again"));
    }

    // --- 2. restoring it after a new connection -----------------------------
    {
        MoondropDevice device(nullptr, new FakeHeadset(FakeHeadset::Edge));
        device.disableStartupAutoConnect();
        int restoreFrames = 0;
        watchRestoreFrames(device, &restoreFrames);

        check(device.ancLastMode() == AncNoiseCancelling,
              QStringLiteral("the remembered mode survives a restart"));
        check(!device.ancRestoreLast(),
              QStringLiteral("restoring is off by default (the headphone's own mode is used)"));

        device.connectDevice();
        check(waitFor([&] { return device.connected(); }), QStringLiteral("second session connects"));
        check(waitFor([&] { return !device.busy(); }, 8000), QStringLiteral("second session refresh finishes"));
        check(device.ancMode() == AncOff,
              QStringLiteral("without the setting the headphone's own mode stays untouched"));
        check(restoreFrames == 0, QStringLiteral("nothing was written back"));

        device.setAncRestoreLast(true);
        device.disconnectDevice();
        device.connectDevice();
        check(waitFor([&] { return device.connected(); }), QStringLiteral("reconnects with restoring on"));
        check(waitFor([&] { return !device.busy(); }, 8000),
              QStringLiteral("restore and its verification finish"));
        check(device.ancMode() == AncNoiseCancelling,
              QStringLiteral("the remembered mode was written back after connecting"));
        check(restoreFrames == 1, QStringLiteral("the restore writes exactly once per connection"));

        // idle time must not touch the mode again
        for (int i = 0; i < 20; ++i) {
            QEventLoop loop;
            QTimer::singleShot(50, &loop, &QEventLoop::quit);
            loop.exec();
        }
        check(restoreFrames == 1 && device.ancMode() == AncNoiseCancelling,
              QStringLiteral("the restore does not repeat while connected"));
        device.disconnectDevice();
    }

    // --- 3. a headphone that keeps the mode is not written to ---------------
    {
        QFile::remove(configPath);
        MoondropDevice device(nullptr, new FakeHeadset(FakeHeadset::Edge));
        device.disableStartupAutoConnect();
        device.setAddress(address);
        device.setChannel(1);
        device.setAncRestoreLast(true);
        int restoreFrames = 0;
        watchRestoreFrames(device, &restoreFrames);

        device.connectDevice();
        check(waitFor([&] { return device.connected(); }), QStringLiteral("third session connects"));
        check(waitFor([&] { return !device.busy(); }, 8000), QStringLiteral("third session refresh finishes"));
        check(device.ancMode() == AncOff, QStringLiteral("no mode is remembered yet, so nothing is restored"));
        check(restoreFrames == 0, QStringLiteral("no write without a remembered mode"));

        device.setAncMode(AncNoiseCancelling);
        check(waitFor([&] { return !device.busy(); }, 6000), QStringLiteral("ANC write completes again"));
        device.refresh();
        check(waitFor([&] { return !device.busy(); }, 8000), QStringLiteral("manual refresh finishes"));
        check(device.ancMode() == AncNoiseCancelling,
              QStringLiteral("a refresh keeps the mode the headphone reports"));
        check(restoreFrames == 0,
              QStringLiteral("a headphone that already reports the remembered mode is not written to"));
        device.disconnectDevice();
    }

    QFile::remove(configPath);

    std::printf("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
