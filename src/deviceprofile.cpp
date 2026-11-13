// SPDX-License-Identifier: GPL-3.0-or-later
#include "deviceprofile.h"

#include "i18n.h"

namespace Moondrop {

namespace {

// Battery ids used by the TWS models: 1 = left, 2 = right, 3 = case.
constexpr int BatteryIdLeft = 1;
constexpr int BatteryIdRight = 2;
constexpr int BatteryIdCase = 3;

QString familyModuleName(DeviceFamily family)
{
    // the exact keys the official app's product catalog uses
    switch (family) {
    case DeviceFamily::Bluetrum:
        return QStringLiteral("bluetrum");
    case DeviceFamily::Qualcomm:
        return QStringLiteral("qualcomm");
    case DeviceFamily::JieLi:
        return QStringLiteral("jieli");
    case DeviceFamily::Airoha:
        return QStringLiteral("airoha");
    case DeviceFamily::Unknown:
        break;
    }
    return QString();
}

QList<DeviceProfile> buildProfiles()
{
    QList<DeviceProfile> profiles;

    // ---- headphones -------------------------------------------------------
    // The official app catalog lists EDGE 2 as another bluetrum/single-value
    // model with a five band user EQ.  It has to come first: the EDGE patterns
    // below are substrings and would otherwise swallow "MOONDROP EDGE 2".
    {
        DeviceProfile edge2;
        edge2.id = QStringLiteral("edge-2");
        edge2.name = QStringLiteral("MOONDROP EDGE 2");
        edge2.namePatterns = {QStringLiteral("MOONDROP EDGE 2")};
        edge2.exactNames = {QStringLiteral("EDGE 2")};
        edge2.isBuds = false;
        edge2.battery = BatteryKind::Single;
        edge2.batteryIds = {0};
        edge2.ancPath = 8; // same module as EDGE, so try AudioCuration first
        edge2.gainOrder = GainOrder::Descending;
        edge2.family = DeviceFamily::Bluetrum;
        edge2.eqBands = 5;
        edge2.selectable = false;
        edge2.notes = QStringLiteral("Catalog data only; no hardware measurement yet");
        profiles.append(edge2);
    }

    {
        DeviceProfile edge;
        edge.id = QStringLiteral("edge");
        edge.name = QStringLiteral("MOONDROP EDGE");
        edge.namePatterns = {QStringLiteral("MOONDROP EDGE"), QStringLiteral("羽翼")};
        // "EDGE" alone would also match any other maker's "EDGE" product
        edge.exactNames = {QStringLiteral("EDGE")};
        edge.isBuds = false;
        edge.battery = BatteryKind::Single;
        edge.batteryIds = {0};
        edge.ancPath = 8; // AUDIO_CURATION
        edge.parametricEq = true;
        edge.gainOrder = GainOrder::Descending;
        edge.verified = true;
        edge.notes = QStringLiteral("Measured on firmware 1.4.0");
        // the official app drives EDGE through its "bluetrum" module, exactly
        // like the NEKOCAKE: both are BT8922E class parts, not Qualcomm ones
        edge.family = DeviceFamily::Bluetrum;
        edge.eqBands = 5;
        profiles.append(edge);
    }

    // ---- TWS: Pudding (case battery, ANC V2) -------------------------------
    {
        DeviceProfile pudding;
        pudding.id = QStringLiteral("pudding");
        pudding.name = QStringLiteral("MOONDROP Pudding");
        pudding.namePatterns = {QStringLiteral("MOONDROP PUDDING"), QStringLiteral("布丁")};
        pudding.exactNames = {QStringLiteral("PUDDING")};
        pudding.isBuds = true;
        pudding.battery = BatteryKind::BudsAndCase;
        pudding.batteryIds = {BatteryIdLeft, BatteryIdRight};
        pudding.ancPath = 32; // ANC V2, values are 0/1/2 like the UI
        pudding.parametricEq = false;
        pudding.gainOrder = GainOrder::Ascending;
        pudding.verified = false;
        pudding.notes = QStringLiteral("Frames follow the published Pudding captures");
        // The official catalog files Pudding under the "jieli" module (not the
        // Qualcomm one) and lists a ten band user EQ with Q 0.3..10.  This
        // project has no JieLi EQ implementation, so the bands stay unused -
        // they are recorded so the UI stops assuming five bands.
        pudding.family = DeviceFamily::JieLi;
        pudding.eqBands = 10;
        pudding.qMin = 0.3;
        pudding.qMax = 10.0;
        profiles.append(pudding);
    }

    // ---- TWS: Robin (buds only, AudioCuration) -----------------------------
    {
        DeviceProfile robin;
        robin.id = QStringLiteral("robin");
        robin.name = QStringLiteral("MOONDROP Robin");
        robin.namePatterns = {QStringLiteral("ROBIN"), QStringLiteral("知更鸟")};
        robin.exactNames = {QStringLiteral("ROBIN'S EARPHONES")};
        robin.isBuds = true;
        robin.battery = BatteryKind::Buds;
        robin.batteryIds = {BatteryIdLeft, BatteryIdRight};
        robin.ancPath = 8; // AUDIO_CURATION with 1/2/4 write values
        robin.parametricEq = false;
        robin.gainOrder = GainOrder::Unknown;
        robin.verified = false;
        robin.notes = QStringLiteral("Frames follow the published Robin protocol");
        robin.family = DeviceFamily::Bluetrum;
        robin.eqBands = 5;
        profiles.append(robin);
    }

    // ---- Space Travel 2 / 2 Ultra (official catalog: bluetrum, TWS, 5 bands) --
    // "SPACE TRAVEL 2" contains "SPACE TRAVEL", so the newer models must be
    // listed before the original one.
    {
        DeviceProfile travel2Ultra;
        travel2Ultra.id = QStringLiteral("space-travel-2-ultra");
        travel2Ultra.name = QStringLiteral("MOONDROP SPACE TRAVEL 2 ULTRA");
        travel2Ultra.namePatterns = {QStringLiteral("SPACE TRAVEL 2 ULTRA")};
        travel2Ultra.exactNames = {};
        travel2Ultra.isBuds = true;
        travel2Ultra.battery = BatteryKind::Buds;
        travel2Ultra.batteryIds = {BatteryIdLeft, BatteryIdRight};
        travel2Ultra.ancPath = 8;
        travel2Ultra.gainOrder = GainOrder::Unknown;
        travel2Ultra.family = DeviceFamily::Bluetrum;
        travel2Ultra.eqBands = 5;
        travel2Ultra.selectable = false;
        travel2Ultra.notes = QStringLiteral("Catalog data only; no hardware measurement yet");
        profiles.append(travel2Ultra);
    }
    {
        DeviceProfile travel2;
        travel2.id = QStringLiteral("space-travel-2");
        travel2.name = QStringLiteral("MOONDROP SPACE TRAVEL 2");
        travel2.namePatterns = {QStringLiteral("SPACE TRAVEL 2")};
        travel2.exactNames = {};
        travel2.isBuds = true;
        travel2.battery = BatteryKind::Buds;
        travel2.batteryIds = {BatteryIdLeft, BatteryIdRight};
        travel2.ancPath = 8;
        travel2.gainOrder = GainOrder::Unknown;
        travel2.family = DeviceFamily::Bluetrum;
        travel2.eqBands = 5;
        travel2.selectable = false;
        travel2.notes = QStringLiteral("Catalog data only; no hardware measurement yet");
        profiles.append(travel2);
    }

    // ---- space travel (ANC V1 style, no PEQ) -------------------------------
    {
        DeviceProfile travel;
        travel.id = QStringLiteral("space-travel");
        travel.name = QStringLiteral("MOONDROP Space Travel");
        travel.namePatterns = {QStringLiteral("SPACE TRAVEL"), QStringLiteral("太空漫游")};
        travel.exactNames = {QStringLiteral("SPACE TRAVEL")};
        travel.isBuds = true;
        travel.battery = BatteryKind::Buds;
        travel.batteryIds = {BatteryIdLeft, BatteryIdRight};
        travel.ancPath = -1; // not measured here: use whatever the device answers
        travel.parametricEq = false;
        travel.gainOrder = GainOrder::Unknown;
        travel.verified = false;
        travel.notes = QStringLiteral("Capabilities are detected from the device");
        travel.family = DeviceFamily::Bluetrum;
        travel.eqBands = 5;
        profiles.append(travel);

        // NEKOCAKE (Bluetrum BT8922E) - measured on firmware 1.0.0.  It is a TWS
        // model but exposes no GAIA battery and no ANC feature at all: the level
        // comes from BlueZ's Battery1 and ANC is switched by holding the earbud.
        // Its own profile keeps that documented (instead of silently reusing the
        // generic fallback, which claims nothing).
        DeviceProfile nekocake;
        nekocake.id = QStringLiteral("nekocake");
        nekocake.name = QStringLiteral("MOONDROP NEKOCAKE");
        nekocake.namePatterns = {QStringLiteral("NEKOCAKE"), QStringLiteral("猫饼")};
        nekocake.exactNames = {};
        nekocake.isBuds = true;
        // measured: no BATTERY feature in the capability bitmap
        nekocake.battery = BatteryKind::Unknown;
        nekocake.batteryIds = {};
        // measured: F2 / F8 / F32 all stay silent on the GET and SET commands
        nekocake.ancPath = -1;
        nekocake.parametricEq = false;
        nekocake.gainOrder = GainOrder::Unknown;
        nekocake.verified = true;
        nekocake.notes = QStringLiteral("Measured on firmware 1.0.0: no GAIA battery (BlueZ is used) "
                                       "and no GAIA ANC (hold the earbud to switch)");
        nekocake.family = DeviceFamily::Bluetrum;
        nekocake.eqBands = 5;
        profiles.append(nekocake);
    }

    // ---- models taken from the official app's product catalog -------------
    // https://cdn-service.moondroplab.tech/api/v1/products/all (2026-11-13).
    // Each row records the module the app uses, whether the model reports one
    // battery or two, and the user EQ band count the catalog lists.  None of
    // these rows has been measured on hardware here: they only make the model
    // match a profile (so battery/EQ handling stops assuming headphones), and
    // the capability probe still decides what the device actually answers.
    auto addCatalog = [&profiles](const char *id, const char *name,
                                  const QStringList &patterns, const QStringList &exacts,
                                  DeviceFamily family, bool buds, BatteryKind battery,
                                  const QList<int> &ids, int eqBands, int ancPath) {
        DeviceProfile profile;
        profile.id = QString::fromLatin1(id);
        profile.name = QString::fromUtf8(name);
        profile.namePatterns = patterns;
        profile.exactNames = exacts;
        profile.isBuds = buds;
        profile.battery = battery;
        profile.batteryIds = ids;
        profile.ancPath = ancPath;
        profile.family = family;
        profile.eqBands = eqBands;
        profile.selectable = false;
        profile.notes = QStringLiteral("Catalog data only; no hardware measurement yet");
        profiles.append(profile);
    };

    const QList<int> budsOnly = {BatteryIdLeft, BatteryIdRight};
    const QList<int> singleValue = {0};

    // bluetrum module, two batteries
    addCatalog("evo-2", "MOONDROP EVO 2",
               {QStringLiteral("MOONDROP EVO 2")}, {QStringLiteral("EVO 2")},
               DeviceFamily::Bluetrum, true, BatteryKind::Buds, budsOnly, 10, 8);
    addCatalog("pill", "MOONDROP Pill",
               {QStringLiteral("MOONDROP PILL"), QStringLiteral("PILL "),
                QStringLiteral("OPEN AIR PILL")},
               {QStringLiteral("PILL")},
               DeviceFamily::Bluetrum, true, BatteryKind::Buds, budsOnly, 5, 8);
    addCatalog("moca", "MOONDROP MOCA",
               {QStringLiteral("MOONDROP MOCA")}, {QStringLiteral("MOCA")},
               DeviceFamily::Bluetrum, true, BatteryKind::Buds, budsOnly, 5, 8);
    addCatalog("golden-ages", "MOONDROP Golden Ages",
               {QStringLiteral("GOLDEN AGES")}, {},
               DeviceFamily::Bluetrum, true, BatteryKind::Buds, budsOnly, 5, 8);
    addCatalog("moon-travel", "MOONDROP MOON TRAVEL",
               {QStringLiteral("MOON TRAVEL")}, {},
               DeviceFamily::Bluetrum, true, BatteryKind::Buds, budsOnly, 5, 8);
    addCatalog("ultrasonic", "MOONDROP Ultrasonic",
               {QStringLiteral("MOONDROP ULTRASONIC")}, {QStringLiteral("ULTRASONIC")},
               DeviceFamily::Bluetrum, true, BatteryKind::Buds, budsOnly, 5, 8);
    addCatalog("susanoo", "MOONDROP SUSANOO TWS",
               {QStringLiteral("SUSANOO")}, {},
               DeviceFamily::Bluetrum, true, BatteryKind::Buds, budsOnly, 5, 8);

    // airoha module: ten band EQ, command family unknown to this project
    addCatalog("the-garden", "MOONDROP The Garden",
               {QStringLiteral("MOONDROP THE GARDEN")}, {QStringLiteral("THE GARDEN")},
               DeviceFamily::Airoha, true, BatteryKind::Buds, budsOnly, 10, -1);

    // qualcomm module: one model reports a single value, two report two
    addCatalog("voyager", "MOONDROP Voyager",
               {QStringLiteral("VOYAGER")}, {},
               DeviceFamily::Qualcomm, false, BatteryKind::Single, singleValue, 5, -1);
    addCatalog("littlewhite", "MOONDROP littlewhite",
               {QStringLiteral("LITTLEWHITE")}, {},
               DeviceFamily::Qualcomm, false, BatteryKind::Single, singleValue, 5, -1);
    addCatalog("alice", "MOONDROP Alice",
               {QStringLiteral("MOONDROP ALICE")}, {QStringLiteral("ALICE")},
               DeviceFamily::Qualcomm, true, BatteryKind::Buds, budsOnly, 5, -1);
    addCatalog("sparks", "MOONDROP Sparks",
               {QStringLiteral("MOONDROP SPARKS")}, {QStringLiteral("SPARKS")},
               DeviceFamily::Qualcomm, true, BatteryKind::Buds, budsOnly, 5, -1);

    // jieli module: ten band EQ with Q 0.3..10 (the app's own limit)
    addCatalog("dots", "MOONDROP Dots",
               {QStringLiteral("MOONDROP DOTS")}, {},
               DeviceFamily::JieLi, true, BatteryKind::Buds, budsOnly, 10, -1);
    addCatalog("mirage", "MOONDROP MIRAGE",
               {QStringLiteral("MOONDROP MIRAGE")}, {QStringLiteral("MIRAGE")},
               DeviceFamily::JieLi, true, BatteryKind::Buds, budsOnly, 10, -1);

    // ---- generic ----------------------------------------------------------
    {
        DeviceProfile generic;
        generic.id = QStringLiteral("generic");
        generic.name = QStringLiteral("MOONDROP headphone");
        generic.namePatterns = {QStringLiteral("MOONDROP"), QStringLiteral("水月雨")};
        generic.exactNames = {};
        generic.isBuds = false;
        generic.battery = BatteryKind::Unknown;
        generic.batteryIds = {BatteryIdLeft, BatteryIdRight};
        generic.ancPath = -1;
        generic.parametricEq = false;
        generic.gainOrder = GainOrder::Unknown;
        generic.verified = false;
        generic.notes = QStringLiteral("Capabilities are detected from the device");
        profiles.append(generic);
    }

    // ---- unknown brand ----------------------------------------------------
    {
        DeviceProfile unknown;
        unknown.id = QStringLiteral("unknown");
        unknown.name = QStringLiteral("Unknown device");
        unknown.namePatterns = {};
        unknown.battery = BatteryKind::Unknown;
        unknown.batteryIds = {BatteryIdLeft, BatteryIdRight};
        profiles.append(unknown);
    }

    return profiles;
}

} // namespace

const QList<DeviceProfile> &deviceProfiles()
{
    static const QList<DeviceProfile> profiles = buildProfiles();
    return profiles;
}

QString profileNotes(const DeviceProfile &profile)
{
    // The literals have to appear here (not be built at runtime) so that the
    // string extractor can find them; DeveloperProfile::notes is the English
    // fallback used when a translation is missing.
    if (profile.id == QLatin1String("edge")) {
        return moondropTr("Measured on a real device (firmware 1.4.0)");
    }
    if (profile.id == QLatin1String("pudding")) {
        return moondropTr("Frames follow the published Pudding captures");
    }
    if (profile.id == QLatin1String("robin")) {
        return moondropTr("Frames follow the published Robin protocol");
    }
    if (profile.id == QLatin1String("space-travel")) {
        return moondropTr("Capabilities are detected from the device");
    }
    if (profile.id == QLatin1String("generic")) {
        return moondropTr("Capabilities are detected from the device");
    }
    if (profile.family != DeviceFamily::Unknown) {
        return moondropTr("Official app catalog: %1 module, %2 EQ bands; commands are not measured on hardware yet.")
            .arg(familyModuleName(profile.family))
            .arg(profile.eqBands > 0 ? QString::number(profile.eqBands) : QStringLiteral("?"));
    }
    return QString();
}

QString profileDisplayName(const DeviceProfile &profile)
{
    // the two fallback entries carry a message id instead of a product name
    if (profile.id == QLatin1String("generic")) {
        return moondropTr("MOONDROP headphone");
    }
    if (profile.id == QLatin1String("unknown")) {
        return moondropTr("Unknown device");
    }
    return profile.name;
}

bool matchesProfile(const DeviceProfile &profile, const QString &modelName)
{
    const QString needle = modelName.toUpper().trimmed();
    if (needle.isEmpty()) {
        return false;
    }
    for (const QString &name : profile.exactNames) {
        if (needle == name) {
            return true;
        }
    }
    for (const QString &pattern : profile.namePatterns) {
        if (needle.contains(pattern)) {
            return true;
        }
    }
    return false;
}

const DeviceProfile &matchProfile(const QString &modelName)
{
    const QList<DeviceProfile> &profiles = deviceProfiles();

    for (int i = 0; i < profiles.size(); ++i) {
        // the last entry is the unknown fallback, it has no names
        if (!matchesProfile(profiles.at(i), modelName)) {
            continue;
        }
        return profiles.at(i);
    }
    // nothing matched: a MOONDROP device we do not know yet still gets the
    // generic treatment, anything else stays unknown
    const QString needle = modelName.toUpper().trimmed();
    if (needle.contains(QLatin1String("MOONDROP")) || needle.contains(QStringLiteral("水月雨"))) {
        return profiles.at(profiles.size() - 2);
    }
    return profiles.last();
}

bool isCaseBattery(int batteryId)
{
    return batteryId == BatteryIdCase;
}

bool batteryLevelIsValid(BatteryKind kind, int batteryId, int level)
{
    if (level < 0 || level > 100) {
        return false;
    }
    switch (kind) {
    case BatteryKind::BudsAndCase:
        // the case reports a real 0 % when it is empty, the buds do not
        if (isCaseBattery(batteryId)) {
            return true;
        }
        return level > 0;
    case BatteryKind::Buds:
        return level > 0;
    case BatteryKind::Single:
    case BatteryKind::Unknown:
    default:
        return true;
    }
}

} // namespace Moondrop
