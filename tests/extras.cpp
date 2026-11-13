// SPDX-License-Identifier: GPL-3.0-or-later
// Extra settings (the official app's feature-gated parameters) against the
// "Rich" fake headset: reads, writes, ANC switch configuration and the
// multipoint device list.
//
//   ./build/cli/moondrop-extras-check
//
// The fake answers with the byte layouts documented in docs/PROTOCOL.md 13.4/13.5
// (the app's V3*Plugin$COMMANDS + data classes), so this pins the parsing and the
// payload building without needing a headphone that exposes all of them.
#include "fakeheadset.h"
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QVariantMap>

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

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 4000)
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

QVariant extraValue(const MoondropDevice &device, const QString &id)
{
    for (const QVariant &item : device.extraSettings()) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("id")).toString() == id) {
            return map.value(QStringLiteral("value"));
        }
    }
    return QVariant();
}

bool hasExtra(const MoondropDevice &device, const QString &id)
{
    return extraValue(device, id).isValid();
}

QVariant gestureAction(const MoondropDevice &device, int gestureId, int touchpad, int context)
{
    for (const QVariant &row : device.gestures()) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("id")).toInt() != gestureId) {
            continue;
        }
        for (const QVariant &item : map.value(QStringLiteral("entries")).toList()) {
            const QVariantMap entry = item.toMap();
            if (entry.value(QStringLiteral("touchpad")).toInt() == touchpad
                && entry.value(QStringLiteral("context")).toInt() == context) {
                return entry.value(QStringLiteral("action"));
            }
        }
    }
    return QVariant();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString configPath = QDir::temp().filePath(QStringLiteral("moondrop-extras-check.ini"));
    QFile::remove(configPath);
    qputenv("MOONDROP_CONFIG", configPath.toLocal8Bit());

    MoondropDevice device(nullptr, new FakeHeadset(FakeHeadset::Rich));
    device.disableStartupAutoConnect();
    QObject::connect(&device, &MoondropDevice::logMessage, [](const QString &message) {
        if (qEnvironmentVariableIsSet("MOONDROP_DEBUG")) {
            std::printf("     %s\n", qPrintable(message));
        }
    });
    device.setAddress(QStringLiteral("00:11:22:33:44:55"));
    device.setChannel(1);

    device.connectDevice();
    check(waitFor([&] { return device.connected(); }), QStringLiteral("connects to the fake headset"));
    check(waitFor([&] { return device.ready() && !device.busy(); }, 8000),
          QStringLiteral("state refresh (including the extras) finishes"));

    // --- capability versions -------------------------------------------------
    int acVersion = -1;
    for (const QVariant &item : device.features()) {
        const QVariantMap feature = item.toMap();
        if (feature.value(QStringLiteral("id")).toInt() == 8) {
            acVersion = feature.value(QStringLiteral("version")).toInt();
        }
    }
    check(acVersion == 4, QStringLiteral("feature versions are kept (AudioCuration v4)"));

    // --- reads ---------------------------------------------------------------
    check(hasExtra(device, QStringLiteral("led")), QStringLiteral("LED is offered"));
    check(hasExtra(device, QStringLiteral("powerOffTimeout")),
          QStringLiteral("power off timeout is offered"));
    check(hasExtra(device, QStringLiteral("windNoise")),
          QStringLiteral("wind noise is offered (AudioCuration v4)"));
    check(hasExtra(device, QStringLiteral("autoTransparency")),
          QStringLiteral("auto transparency is offered"));
    check(hasExtra(device, QStringLiteral("multipointTimeout")),
          QStringLiteral("multipoint timeout is offered"));
    check(device.extraSettings().size() == 13,
          QStringLiteral("all 13 supported settings are listed"));

    check(extraValue(device, QStringLiteral("led")).toBool() == false,
          QStringLiteral("LED reads 0"));
    check(extraValue(device, QStringLiteral("sensor")).toBool() == true,
          QStringLiteral("sensor reads 1"));
    check(extraValue(device, QStringLiteral("dybass")).toBool() == true,
          QStringLiteral("dynamic bass reads 1"));
    check(extraValue(device, QStringLiteral("lrReversed")).toBool() == false,
          QStringLiteral("LR swap reads 0"));
    check(extraValue(device, QStringLiteral("powerOffTimeout")).toInt() == 0,
          QStringLiteral("power off timeout reads 0 (disabled)"));
    check(extraValue(device, QStringLiteral("standbyTimeout")).toInt() == 30,
          QStringLiteral("standby timeout reads 30 minutes"));
    check(extraValue(device, QStringLiteral("acGain")).toInt() == 128,
          QStringLiteral("AudioCuration gain reads 128"));
    check(extraValue(device, QStringLiteral("multipointTimeout")).toInt() == 30,
          QStringLiteral("multipoint timeout reads 30"));

    const QVariantMap switchConfig = device.ancSwitchConfig();
    check(switchConfig.value(QStringLiteral("present")).toBool(),
          QStringLiteral("ANC switch configuration is read"));
    check(switchConfig.value(QStringLiteral("state")).toBool()
              && switchConfig.value(QStringLiteral("ancOn")).toBool()
              && switchConfig.value(QStringLiteral("ancOff")).toBool()
              && switchConfig.value(QStringLiteral("transparency")).toBool()
              && switchConfig.value(QStringLiteral("order")).toInt() == 1,
          QStringLiteral("ANC switch bytes decode (all on, order OFF_ON_TP)"));

    check(device.multipointDevices().size() == 1,
          QStringLiteral("first multipoint device is listed"));
    if (device.multipointDevices().size() == 1) {
        const QVariantMap first = device.multipointDevices().first().toMap();
        check(first.value(QStringLiteral("name")).toString() == QLatin1String("Phone"),
              QStringLiteral("device name is decoded"));
        check(first.value(QStringLiteral("address")).toString() == QLatin1String("D4:17:61:B2:92:D4"),
              QStringLiteral("device address is decoded"));
    }
    device.requestNextMultipointDevice();
    check(waitFor([&] { return !device.busy(); }), QStringLiteral("next device request finishes"));
    check(device.multipointDevices().size() == 2,
          QStringLiteral("second multipoint device is appended"));

    // --- writes --------------------------------------------------------------
    device.setExtra(QStringLiteral("led"), true);
    check(waitFor([&] { return !device.busy(); }), QStringLiteral("LED write finishes"));
    check(extraValue(device, QStringLiteral("led")).toBool() == true,
          QStringLiteral("LED write is confirmed by a read back"));

    device.setExtra(QStringLiteral("powerOffTimeout"), 30);
    check(waitFor([&] { return !device.busy(); }), QStringLiteral("timeout write finishes"));
    check(extraValue(device, QStringLiteral("powerOffTimeout")).toInt() == 30,
          QStringLiteral("power off timeout is stored"));

    device.setExtra(QStringLiteral("acGain"), 200);
    check(waitFor([&] { return !device.busy(); }), QStringLiteral("AC gain write finishes"));
    check(extraValue(device, QStringLiteral("acGain")).toInt() == 200,
          QStringLiteral("AC gain is stored"));

    device.setAncSwitchConfig(false, true, false, true, 0);
    check(waitFor([&] { return !device.busy(); }), QStringLiteral("ANC switch write finishes"));
    const QVariantMap updated = device.ancSwitchConfig();
    check(updated.value(QStringLiteral("state")).toBool() == false
              && updated.value(QStringLiteral("ancOff")).toBool() == false
              && updated.value(QStringLiteral("order")).toInt() == 0,
          QStringLiteral("ANC switch configuration is written and read back"));

    device.disconnectMultipointDevice(1);
    check(waitFor([&] { return !device.busy(); }), QStringLiteral("multipoint disconnect finishes"));
    check(device.multipointDevices().size() == 1
              && device.multipointDevices().first().toMap().value(QStringLiteral("name")).toString()
                     == QLatin1String("Laptop"),
          QStringLiteral("the disconnected device is gone after the refresh"));

    // --- button gestures (feature 11) ----------------------------------------
    check(device.gestures().size() == 8, QStringLiteral("all eight gestures are read"));
    if (!device.gestures().isEmpty()) {
        const QVariantMap first = device.gestures().first().toMap();
        check(first.value(QStringLiteral("entries")).toList().size() == 9,
              QStringLiteral("the first gesture has nine context entries"));
    }
    check(device.gestureActionOptions().size() == 27,
          QStringLiteral("27 actions are offered"));

    device.setGestureAction(0, 0, 0, 2);
    check(waitFor([&] { return !device.busy(); }), QStringLiteral("gesture write finishes"));
    const QVariant action = gestureAction(device, 0, 0, 0);
    check(action.toInt() == 2, QStringLiteral("gesture action is written and read back"));

    device.resetGestureConfiguration();
    check(waitFor([&] { return gestureAction(device, 0, 0, 0).toInt() == 0; }, 6000),
          QStringLiteral("reset restores the default gesture action"));

    std::printf(failures == 0 ? "\nall checks passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
