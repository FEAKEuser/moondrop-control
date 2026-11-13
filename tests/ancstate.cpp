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
#include <QStringList>
#include <QTimer>

#include <algorithm>
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

// every protocol log line, for checks that look at what went over the wire
void watchLog(MoondropDevice &device, QStringList *lines)
{
    QObject::connect(&device, &MoondropDevice::logMessage, [lines](const QString &message) {
        lines->append(message);
    });
}

bool sentFrame(const QStringList &lines, const QString &featureCommandHex)
{
    return std::any_of(lines.cbegin(), lines.cend(), [&featureCommandHex](const QString &line) {
        return line.startsWith(QLatin1String("TX ")) && line.contains(featureCommandHex);
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
        auto *fake = new FakeHeadset(FakeHeadset::Edge);
        MoondropDevice device(nullptr, fake);
        device.disableStartupAutoConnect();
        int restoreFrames = 0;
        watchRestoreFrames(device, &restoreFrames);

        check(device.ancLastMode() == AncNoiseCancelling,
              QStringLiteral("the remembered mode survives a restart"));
        check(!device.ancRestoreLast(),
              QStringLiteral("restoring is off by default (the headphone's own mode is used)"));

        QStringList log;
        watchLog(device, &log);

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
        check(std::none_of(log.cbegin(), log.cend(), [](const QString &line) {
                  return line.contains(QLatin1String("no answer for anc-restore"));
              }),
              QStringLiteral("the unacknowledged restore write is not reported as a failure"));

        // idle time must not touch the mode again
        for (int i = 0; i < 20; ++i) {
            QEventLoop loop;
            QTimer::singleShot(50, &loop, &QEventLoop::quit);
            loop.exec();
        }
        check(restoreFrames == 1 && device.ancMode() == AncNoiseCancelling,
              QStringLiteral("the restore does not repeat while connected"));

        // The decision for this connection is final: a mode switched on the
        // headphone itself after the restore must survive a refresh, even though
        // the remembered mode is still the old one.
        fake->setAncMode(AncOff);
        device.refresh();
        check(waitFor([&] { return !device.busy(); }, 8000),
              QStringLiteral("refresh after a hand change finishes"));
        check(device.ancMode() == AncOff,
              QStringLiteral("a hand change after the restore survives a refresh"));
        check(restoreFrames == 1,
              QStringLiteral("the already made decision is not revisited by a refresh"));
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

        // reconnect while the headphone is already in the remembered mode: the
        // decision has to stop at the comparison, not write "the same" again
        device.disconnectDevice();
        device.connectDevice();
        check(waitFor([&] { return device.connected(); }), QStringLiteral("reconnects in the remembered mode"));
        check(waitFor([&] { return !device.busy(); }, 8000),
              QStringLiteral("refresh in the remembered mode finishes"));
        check(device.ancMode() == AncNoiseCancelling && restoreFrames == 0,
              QStringLiteral("a matching mode is not written on the next connection either"));
        device.disconnectDevice();
    }

    // --- 4. a model that acknowledges the set still gets its settling time -----
    // The EDGE does not answer the ANC set, so the widget's own wait used to
    // cover the engine switch.  A model that *does* answer releases the queue
    // immediately, and verifying right then would read the old mode and show it
    // (and the restore would look like it failed).
    {
        QFile::remove(configPath);
        {
            MoondropDevice device(nullptr, new FakeHeadset(FakeHeadset::Edge));
            device.disableStartupAutoConnect();
            device.setAddress(address);
            device.setChannel(1);
            device.setAncRestoreLast(true);
            device.connectDevice();
            check(waitFor([&] { return device.connected(); }), QStringLiteral("remembering: connects"));
            check(waitFor([&] { return !device.busy(); }), QStringLiteral("remembering: refresh done"));
            device.setAncMode(AncNoiseCancelling);
            check(waitFor([&] { return !device.busy(); }, 6000),
                  QStringLiteral("remembering: the mode is set"));
            device.disconnectDevice();
        }

        auto *acked = new FakeHeadset(FakeHeadset::Edge);
        acked->setAncSetAckDelay(1500); // acknowledges now, mode live 1.5 s later

        MoondropDevice device(nullptr, acked);
        device.disableStartupAutoConnect();
        check(device.ancLastMode() == AncNoiseCancelling,
              QStringLiteral("acking model: the mode is remembered"));
        device.connectDevice();
        check(waitFor([&] { return device.connected(); }), QStringLiteral("acking model: connects"));
        check(waitFor([&] { return !device.busy(); }, 8000),
              QStringLiteral("acking model: restore and verification finish"));
        check(device.ancMode() == AncNoiseCancelling,
              QStringLiteral("acking model: verified only after the engine had time to switch"));
        device.disconnectDevice();
    }

    // --- 5. flipping the switch mid-session leaves the running link alone -----
    // The restore behaviour is fixed when the link comes up.  Enabling it while
    // the headphone is already connected must not rewrite a mode the user set
    // afterwards (on the headphone itself, for example).
    {
        QFile::remove(configPath);
        auto *fake = new FakeHeadset(FakeHeadset::Edge);
        MoondropDevice device(nullptr, fake);
        device.disableStartupAutoConnect();
        device.setAddress(address);
        device.setChannel(1);

        // session 1: remember ANC (the switch is still off)
        device.connectDevice();
        check(waitFor([&] { return device.connected(); }), QStringLiteral("touch: first session connects"));
        check(waitFor([&] { return !device.busy(); }), QStringLiteral("touch: first session refresh"));
        device.setAncMode(AncNoiseCancelling);
        check(waitFor([&] { return !device.busy(); }, 6000), QStringLiteral("touch: ANC is remembered"));
        device.disconnectDevice();

        // session 2: connect with the switch off, then change the mode by hand
        int restoreFrames = 0;
        watchRestoreFrames(device, &restoreFrames);
        device.connectDevice();
        check(waitFor([&] { return device.connected(); }), QStringLiteral("touch: second session connects"));
        check(waitFor([&] { return !device.busy(); }), QStringLiteral("touch: second session refresh"));
        check(device.ancMode() == AncNoiseCancelling, QStringLiteral("touch: the headphone reports the remembered mode"));

        fake->setAncMode(AncTransparency); // on the headphone itself
        device.setAncRestoreLast(true); // enabled while this link is already up
        device.refresh();
        check(waitFor([&] { return !device.busy(); }, 8000), QStringLiteral("touch: refresh finishes"));
        check(device.ancMode() == AncTransparency,
              QStringLiteral("the switch turned on mid-session does not rewrite the running connection"));
        check(restoreFrames == 0, QStringLiteral("no restore frame was written inside the running connection"));
        device.disconnectDevice();
    }

    // --- 6. the ANC V1 family (feature 2) ----------------------------------
    // No measured model implements it, so nothing but a fake can cover the path;
    // that is exactly why it is here: the write/read/verify mapping is only
    // claimed by the protocol notes.
    {
        QFile::remove(configPath);
        auto *v1 = new FakeHeadset(FakeHeadset::AncV1);
        MoondropDevice device(nullptr, v1);
        device.disableStartupAutoConnect();
        device.setAddress(address);
        device.setChannel(1);

        QStringList log;
        watchLog(device, &log);

        device.connectDevice();
        check(waitFor([&] { return device.connected(); }), QStringLiteral("V1: connects"));
        check(waitFor([&] { return !device.busy(); }), QStringLiteral("V1: refresh finishes"));
        check(device.ancPath() == AncPathAncV1, QStringLiteral("V1: the ANC command family is detected"));
        check(device.ancMode() == AncOff, QStringLiteral("V1: the reported mode is off"));

        log.clear();
        device.setAncMode(AncNoiseCancelling);
        check(waitFor([&] { return !device.busy(); }, 6000), QStringLiteral("V1: the ANC switch completes"));
        check(device.ancMode() == AncNoiseCancelling, QStringLiteral("V1: the mode is applied and verified"));
        check(sentFrame(log, QStringLiteral("1d 04 04")),
              QStringLiteral("V1: the verification read uses feature 2 (command 4)"));

        device.setAncMode(AncTransparency);
        check(waitFor([&] { return !device.busy(); }, 6000), QStringLiteral("V1: the transparency switch completes"));
        check(device.ancMode() == AncNoiseCancelling,
              QStringLiteral("V1: transparency folds into the on/off value the family has"));

        device.disconnectDevice();
        v1->setAncMode(AncOff); // switched on the headphone while disconnected

        device.setAncRestoreLast(true);
        log.clear();
        device.connectDevice();
        check(waitFor([&] { return device.connected(); }), QStringLiteral("V1: reconnects"));
        check(waitFor([&] { return !device.busy(); }, 8000),
              QStringLiteral("V1: the restore and its verification finish"));
        check(device.ancMode() == AncNoiseCancelling, QStringLiteral("V1: the remembered mode is restored"));
        check(sentFrame(log, QStringLiteral("1d 04 02")),
              QStringLiteral("V1: the restore writes the V1 on value (SET_ANC_STATE = 2)"));
        device.disconnectDevice();
    }

    QFile::remove(configPath);

    std::printf("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
