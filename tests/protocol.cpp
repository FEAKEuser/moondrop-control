// SPDX-License-Identifier: MIT
// Self test for the pure protocol helpers (no Bluetooth involved):
//
//   ./build/cli/moondrop-selftest
//
// The 39 byte PEQ payload below is the one a real MOONDROP EDGE (firmware 1.4.0)
// reports for its custom curve, so the codec is checked against hardware output.
#include "deviceprofile.h"
#include "gaia.h"
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QDebug>

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

void testFraming()
{
    // The command word carries the packet type in bits 7-8: the phone sends
    // COMMAND (0), the headset answers with RESPONSE (2).  A firmware version
    // query therefore looks like this (captured from the real link):
    const QByteArray query = encodeFrame(FeatureBasic, 5);
    check(query == QByteArray::fromHex("ff040000001d0005"),
          QStringLiteral("encodeFrame(BASIC, 5) == ff 04 00 00 00 1d 00 05 (COMMAND)"));

    Stream queryStream;
    const QList<Frame> queryFrames = queryStream.feed(query);
    check(queryFrames.size() == 1 && queryFrames.first().type() == TypeCommand
              && queryFrames.first().feature() == FeatureBasic
              && queryFrames.first().cmd() == 5,
          QStringLiteral("our own frame parses back as BASIC/COMMAND/5"));

    // ANC mode request on the AudioCuration feature: (8 << 9) | 3 -> 0x1003
    check(encodeFrame(FeatureAudioCuration, 3) == QByteArray::fromHex("ff040000001d1003"),
          QStringLiteral("encodeFrame(AUDIO_CURATION, 3) == ... 1d 10 03"));

    // the answer of a real device
    Stream stream;
    const QList<Frame> frames = stream.feed(QByteArray::fromHex("ff040005001d0105312e342e30"));
    check(frames.size() == 1, QStringLiteral("one frame parsed"));
    if (frames.size() == 1) {
        const Frame &frame = frames.first();
        check(frame.vendor == VendorMoondrop, QStringLiteral("vendor == 0x001D"));
        check(frame.feature() == FeatureBasic, QStringLiteral("feature == BASIC"));
        check(frame.cmd() == 5, QStringLiteral("command == 5"));
        check(frame.type() == TypeResponse, QStringLiteral("type == RESPONSE"));
        check(frame.payload == "1.4.0", QStringLiteral("payload == \"1.4.0\""));
    }

    // fragmented delivery must not lose or duplicate frames
    Stream fragmented;
    const QByteArray raw = QByteArray::fromHex("ff040005001d0105312e342e30ff040001001d020201");
    QList<Frame> collected;
    for (int i = 0; i < raw.size(); ++i) {
        collected += fragmented.feed(raw.mid(i, 1));
    }
    check(collected.size() == 2, QStringLiteral("byte-by-byte delivery yields both frames"));

    // garbage before the start byte is skipped
    Stream noisy;
    const QList<Frame> recovered = noisy.feed(QByteArray("\x01\x02\x03", 3) + QByteArray::fromHex("ff040005001d0105312e342e30"));
    check(recovered.size() == 1, QStringLiteral("resynchronises after garbage"));

    // a truncated frame is buffered, not dropped
    Stream truncated;
    check(truncated.feed(QByteArray::fromHex("ff040005001d0105")).isEmpty(),
          QStringLiteral("incomplete frame is buffered"));
    check(truncated.feed(QByteArray::fromHex("312e342e30")).size() == 1,
          QStringLiteral("buffered frame completes"));
}

void testEqCodec()
{
    // custom curve as reported by a real MOONDROP EDGE 1.4.0
    const QByteArray payload =
        QByteArray::fromHex("000400000017066600ffa600f051990000a2057864cc0000b408fc1ccc00ff521af413330000b4");
    const QList<EqBand> bands = decodeUserEq(payload);
    check(bands.size() == 5, QStringLiteral("decoded 5 PEQ bands"));
    if (bands.size() == 5) {
        check(bands[0].frequency == 23, QStringLiteral("band 1 frequency 23 Hz"));
        check(qAbs(bands[0].q - 0.40) < 0.001, QStringLiteral("band 1 Q 0.40"));
        check(qAbs(bands[0].gain - (-1.5)) < 0.001, QStringLiteral("band 1 gain -1.5 dB"));
        check(bands[1].frequency == 240 && qAbs(bands[1].gain - 2.7) < 0.001,
              QStringLiteral("band 2 240 Hz +2.7 dB"));
        check(bands[2].frequency == 1400 && qAbs(bands[2].gain - 3.0) < 0.001,
              QStringLiteral("band 3 1400 Hz +3.0 dB"));
        check(bands[3].frequency == 2300 && qAbs(bands[3].gain - (-2.9)) < 0.001,
              QStringLiteral("band 4 2300 Hz -2.9 dB"));
        check(bands[4].frequency == 6900 && qAbs(bands[4].gain - 3.0) < 0.001,
              QStringLiteral("band 5 6900 Hz +3.0 dB"));
    }

    // re-encoding has to reproduce the device's bytes exactly (the firmware
    // echoes the payload it was given)
    check(encodeUserEq(bands) == payload, QStringLiteral("re-encoding reproduces the device payload"));

    // extreme values stay inside the encoding limits
    QList<EqBand> extremes;
    for (int i = 0; i < 5; ++i) {
        EqBand band;
        band.frequency = 20000;
        band.q = 10.0;
        band.gain = (i % 2 == 0) ? 12.0 : -12.0;
        extremes.append(band);
    }
    const QList<EqBand> roundTrip = decodeUserEq(encodeUserEq(extremes));
    check(roundTrip.size() == 5, QStringLiteral("extremes survive a round trip"));
    if (roundTrip.size() == 5) {
        check(roundTrip[0].frequency == 20000, QStringLiteral("20000 Hz survives"));
        check(qAbs(roundTrip[0].q - 10.0) < 0.001, QStringLiteral("Q 10 survives"));
        check(qAbs(roundTrip[0].gain - 12.0) < 0.01, QStringLiteral("+12 dB survives"));
        check(qAbs(roundTrip[1].gain - (-12.0)) < 0.01, QStringLiteral("-12 dB survives"));
    }

    check(decodeUserEq(QByteArray(10, '\0')).isEmpty(), QStringLiteral("short payload is rejected"));
    check(encodeUserEq({}).isEmpty(), QStringLiteral("wrong band count is rejected"));
}

void testProfiles()
{
    auto idOf = [](const QString &name) { return matchProfile(name).id; };

    check(idOf(QStringLiteral("MOONDROP EDGE")) == QLatin1String("edge"),
          QStringLiteral("profile: MOONDROP EDGE"));
    check(idOf(QStringLiteral("MOONDROP Pudding")) == QLatin1String("pudding"),
          QStringLiteral("profile: MOONDROP Pudding"));
    check(idOf(QStringLiteral("Robin's Earphones")) == QLatin1String("robin"),
          QStringLiteral("profile: Robin's Earphones (published name)"));
    check(idOf(QStringLiteral("MOONDROP Robin")) == QLatin1String("robin"),
          QStringLiteral("profile: MOONDROP Robin"));
    check(idOf(QStringLiteral("\u6c34\u6708\u96e8 \u77e5\u66f4\u9e1f")) == QLatin1String("robin"),
          QStringLiteral("profile: Chinese Robin name"));
    check(idOf(QStringLiteral("MOONDROP Something New")) == QLatin1String("generic"),
          QStringLiteral("profile: unknown MOONDROP model falls back to generic"));
    check(idOf(QStringLiteral("Sony WH-1000XM5")) == QLatin1String("unknown"),
          QStringLiteral("profile: foreign device is unknown"));

    const DeviceProfile edge = matchProfile(QStringLiteral("MOONDROP EDGE"));
    check(edge.verified && edge.parametricEq && edge.gainOrder == GainOrder::Descending,
          QStringLiteral("profile: EDGE is the measured one (PEQ, reversed gain)"));

    const DeviceProfile pudding = matchProfile(QStringLiteral("MOONDROP Pudding"));
    check(pudding.battery == BatteryKind::BudsAndCase && pudding.ancPath == 32,
          QStringLiteral("profile: Pudding has a case battery and ANC V2"));

    // A too generic pattern made "Samsung EDGE 5G" look like a MOONDROP EDGE and
    // receive headphones-only commands.  Short names must match the whole name.
    check(idOf(QStringLiteral("EDGE")) == QLatin1String("edge"),
          QStringLiteral("profile: bare \"EDGE\" still matches"));
    check(idOf(QStringLiteral("Pudding")) == QLatin1String("pudding"),
          QStringLiteral("profile: bare \"Pudding\" still matches"));
    check(idOf(QStringLiteral("SoundPEATS EDGE")) == QLatin1String("unknown"),
          QStringLiteral("profile: another maker's EDGE is not ours"));
    check(idOf(QStringLiteral("Samsung EDGE 5G")) == QLatin1String("unknown"),
          QStringLiteral("profile: a phone named EDGE is not ours"));
    check(idOf(QStringLiteral("EDGE Router")) == QLatin1String("unknown"),
          QStringLiteral("profile: an unrelated EDGE device is not ours"));
    check(idOf(QString::fromUtf8("\u6c34\u6708\u96e8 \u5e03\u4e01")) == QLatin1String("pudding"),
          QStringLiteral("profile: Chinese Pudding name"));

    const DeviceProfile robin = matchProfile(QStringLiteral("MOONDROP Robin"));
    check(robin.battery == BatteryKind::Buds && robin.ancPath == 8,
          QStringLiteral("profile: Robin has buds only and AudioCuration"));

    // battery semantics: on TWS models 0 means "not connected", the case may
    // legitimately report 0 %
    check(!batteryLevelIsValid(BatteryKind::Buds, 1, 0), QStringLiteral("buds: 0 % means not connected"));
    check(batteryLevelIsValid(BatteryKind::Buds, 1, 50), QStringLiteral("buds: 50 % is valid"));
    check(!batteryLevelIsValid(BatteryKind::Buds, 1, 255), QStringLiteral("buds: 0xFF is unreadable"));
    check(batteryLevelIsValid(BatteryKind::BudsAndCase, 3, 0), QStringLiteral("case: 0 % is a real state"));
    check(!batteryLevelIsValid(BatteryKind::BudsAndCase, 3, 255), QStringLiteral("case: 0xFF is unreadable"));
    check(batteryLevelIsValid(BatteryKind::Single, 0, 0), QStringLiteral("headphones: 0 % is valid"));
    check(isCaseBattery(3) && !isCaseBattery(1), QStringLiteral("battery id 3 is the case"));

    // every profile that exposes a parametric EQ must say how its gain steps are
    // numbered, otherwise the UI has no way to label them
    for (const DeviceProfile &profile : deviceProfiles()) {
        if (profile.parametricEq) {
            check(profile.gainOrder != GainOrder::Unknown,
                  QStringLiteral("profile %1 with PEQ declares its gain order").arg(profile.id));
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testFraming();
    testEqCodec();
    testProfiles();

    std::printf("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
