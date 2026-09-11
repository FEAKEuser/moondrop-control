// SPDX-License-Identifier: GPL-3.0-or-later
// Connection and queue behaviour, run against the fake headset (no Bluetooth):
//
//   ./build/cli/moondrop-conncheck
//
// This guards against a class of bug that made the widget unusable in practice:
// a request queued while there is nothing to talk to used to stay pending
// forever, which kept `busy` true - and since the UI disables its controls while
// busy, the user could no longer reconnect.
#include "deviceprofile.h"
#include "fakeheadset.h"
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QEventLoop>
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

// spins the event loop until the predicate holds or the timeout expires
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

bool deviceDone(MoondropDevice &device)
{
    return !device.busy();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // never touch the user's configuration: use a scratch file with defaults
    const QString configPath = QDir::temp().filePath(QStringLiteral("moondrop-conncheck.ini"));
    QFile::remove(configPath);
    qputenv("MOONDROP_CONFIG", configPath.toLocal8Bit());

    MoondropDevice device(nullptr, new FakeHeadset(FakeHeadset::Edge));
    // the automatic connection is exercised separately below
    device.disableStartupAutoConnect();
    int framesSent = 0;
    QObject::connect(&device, &MoondropDevice::logMessage, [&framesSent](const QString &message) {
        if (message.startsWith(QLatin1String("TX "))) {
            ++framesSent;
        }
        if (qEnvironmentVariableIsSet("MOONDROP_DEBUG")) {
            std::printf("     %s\n", qPrintable(message));
        }
    });
    device.setAddress(QStringLiteral("00:11:22:33:44:55"));
    device.setChannel(1);

    // --- 1. requests while disconnected must not be queued -------------------
    device.selectPreset(1);
    device.setAncMode(AncNoiseCancelling);
    device.setLdacEnabled(true);
    device.setDacGain(2);
    check(!device.busy(),
          QStringLiteral("requests made while disconnected are dropped, not queued"));
    check(!device.connected(), QStringLiteral("still disconnected"));

    // --- 2. connecting works afterwards -------------------------------------
    device.connectDevice();
    check(waitFor([&] { return device.connected(); }),
          QStringLiteral("connects to the fake headset"));
    check(waitFor([&] { return deviceDone(device); }),
          QStringLiteral("initial state refresh finishes"));
    check(device.model() == QLatin1String("MOONDROP EDGE"),
          QStringLiteral("model name arrives"));
    check(device.profileId() == QLatin1String("edge"),
          QStringLiteral("profile is matched from the model name"));
    check(device.bands().size() == 5, QStringLiteral("5 PEQ bands are read"));

    // --- 3. writes while connected ------------------------------------------
    device.setAncMode(AncTransparency);
    check(waitFor([&] { return !device.busy(); }), QStringLiteral("ANC write completes"));
    check(device.ancMode() == AncTransparency, QStringLiteral("ANC mode is applied"));

    device.selectPreset(2);
    check(waitFor([&] { return !device.busy(); }), QStringLiteral("preset write completes"));
    check(device.currentPreset() == 2, QStringLiteral("preset is applied"));

    // --- 4. the actual regression ------------------------------------------
    // A request issued right after losing the connection must not leave the
    // device busy forever, otherwise the UI can never reconnect.
    device.disconnectDevice();
    check(!device.connected(), QStringLiteral("disconnected"));
    device.selectPreset(0);
    device.setAncMode(AncOff);
    device.refresh();
    check(!device.busy(), QStringLiteral("requests after a disconnect do not stick"));

    device.connectDevice();
    check(waitFor([&] { return device.connected(); }),
          QStringLiteral("can reconnect after those requests"));
    check(waitFor([&] { return deviceDone(device); }),
          QStringLiteral("state refresh works after reconnecting"));
    check(!device.lastError().isEmpty() == false,
          QStringLiteral("no error left over from the previous attempts"));

    // --- 5. connecting twice is harmless ------------------------------------
    device.connectDevice();
    check(waitFor([&] { return deviceDone(device); }),
          QStringLiteral("a repeated connect request is harmless"));
    check(device.connected(), QStringLiteral("still connected"));

    // --- 6. PEQ write round trip -------------------------------------------
    QVariantList bands;
    const QList<int> frequencies{23, 240, 1400, 2300, 6900};
    const QList<double> gains{3.0, 2.7, 3.0, -2.9, 3.0};
    for (int i = 0; i < frequencies.size(); ++i) {
        QVariantMap band;
        band.insert(QStringLiteral("frequency"), frequencies.at(i));
        band.insert(QStringLiteral("gain"), gains.at(i));
        band.insert(QStringLiteral("q"), 1.0);
        band.insert(QStringLiteral("type"), 0);
        bands.append(band);
    }
    device.applyUserEq(bands, true);
    check(waitFor([&] { return deviceDone(device); }, 5000),
          QStringLiteral("PEQ write and verification finish"));
    check(device.pendingBands().isEmpty(),
          QStringLiteral("PEQ write was confirmed by the device"));
    check(device.currentPreset() == 0x3F,
          QStringLiteral("the custom preset is selected after a PEQ write"));
    check(device.bands().size() == 5 && qAbs(device.bands().at(0).toMap().value(QStringLiteral("gain")).toDouble() - 3.0) < 0.01,
          QStringLiteral("the written curve is what the device reports"));

    // --- 7. no notification ping-pong --------------------------------------
    // Reading a feature can make the device emit a notification.  Reacting to
    // that notification with another read used to loop forever, which kept the
    // queue (and `busy`) permanently filled - the widget then refused every
    // action and looked "unable to connect".
    device.refresh();
    waitFor([&] { return deviceDone(device); }, 8000);
    const int framesBeforeQuiet = framesSent;
    bool stayedIdle = true;
    for (int i = 0; i < 30; ++i) {
        QEventLoop loop;
        QTimer::singleShot(50, &loop, &QEventLoop::quit);
        loop.exec();
        if (device.busy()) {
            stayedIdle = false;
            break;
        }
    }
    check(stayedIdle, QStringLiteral("the device goes idle and stays idle (no notification loop)"));
    check(framesSent - framesBeforeQuiet == 0,
          QStringLiteral("nothing is sent while idle (%1 frames)").arg(framesSent - framesBeforeQuiet));

    device.disconnectDevice();

    // --- 7b. the EQ restore point ------------------------------------------
    // A curve read from the headphone is remembered, so an accidental change can
    // be undone.  (The notification test above disconnected the device.)
    device.connectDevice();
    check(waitFor([&] { return device.connected(); }, 4000),
          QStringLiteral("reconnected for the EQ restore check"));
    check(waitFor([&] { return deviceDone(device); }, 5000), QStringLiteral("state refresh done"));
    check(device.hasDeviceEqBackup(),
          QStringLiteral("the curve reported by the headphone is kept as a restore point"));
    QVariantList flattened;
    const QList<int> flatFreqs{23, 240, 1400, 2300, 6900};
    for (int frequency : flatFreqs) {
        QVariantMap band;
        band.insert(QStringLiteral("frequency"), frequency);
        band.insert(QStringLiteral("gain"), 0.0);
        band.insert(QStringLiteral("q"), 1.0);
        band.insert(QStringLiteral("type"), 0);
        flattened.append(band);
    }
    device.applyUserEq(flattened, true);
    check(waitFor([&] { return deviceDone(device); }, 5000), QStringLiteral("flatten completes"));
    check(qAbs(device.bands().at(0).toMap().value(QStringLiteral("gain")).toDouble()) < 0.01,
          QStringLiteral("flatten really flattened the curve"));
    check(device.hasDeviceEqBackup(),
          QStringLiteral("the previous curve is still restorable after flattening"));

    device.restoreDeviceEq();
    check(waitFor([&] { return deviceDone(device); }, 5000), QStringLiteral("restore completes"));
    check(qAbs(device.bands().at(0).toMap().value(QStringLiteral("gain")).toDouble() - 3.0) < 0.01,
          QStringLiteral("restore brought the previous curve back"));

    // --- 8. connecting without the user pressing anything -------------------
    // The applet relies on this: on startup the backend links the control
    // channel by itself, and it keeps trying (with a growing delay) while the
    // headphone is off.
    {
        MoondropDevice automatic(nullptr, new FakeHeadset(FakeHeadset::Edge));
        automatic.setAddress(QStringLiteral("00:11:22:33:44:55"));
        automatic.setChannel(1);
        check(!automatic.connected(), QStringLiteral("automatic: starts disconnected"));
        check(waitFor([&] { return automatic.connected(); }, 4000),
              QStringLiteral("automatic: connects on its own after startup"));
        check(waitFor([&] { return !automatic.busy(); }, 5000),
              QStringLiteral("automatic: finishes the state refresh"));

        // and it recovers from a lost link without user interaction
        automatic.disconnectDevice();
        check(!automatic.connected(), QStringLiteral("automatic: disconnected again"));
        automatic.connectDevice();
        check(waitFor([&] { return automatic.connected(); }, 6000),
              QStringLiteral("automatic: reconnects without user interaction"));
        check(waitFor([&] { return !automatic.busy(); }, 5000),
              QStringLiteral("automatic: state refresh after the reconnect"));
        automatic.disconnectDevice();
    }

    QFile::remove(configPath);

    std::printf("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
