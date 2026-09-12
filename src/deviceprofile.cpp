// SPDX-License-Identifier: GPL-3.0-or-later
#include "deviceprofile.h"

#include "i18n.h"

namespace Moondrop {

namespace {

// Battery ids used by the TWS models: 1 = left, 2 = right, 3 = case.
constexpr int BatteryIdLeft = 1;
constexpr int BatteryIdRight = 2;
constexpr int BatteryIdCase = 3;

QList<DeviceProfile> buildProfiles()
{
    QList<DeviceProfile> profiles;

    // ---- headphones -------------------------------------------------------
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
        profiles.append(robin);
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
        profiles.append(nekocake);
    }

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
