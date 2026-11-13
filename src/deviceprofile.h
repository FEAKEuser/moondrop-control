// SPDX-License-Identifier: GPL-3.0-or-later
// Per-model quirks.
//
// All current MOONDROP models speak the same GAIA framing over RFCOMM, but they
// differ in details: which ANC command family they answer, how the battery report
// is laid out, whether there is a parametric EQ and how the gain steps are
// numbered.  A profile captures exactly those differences so the rest of the code
// can stay model agnostic.
//
// Sources: own measurement on MOONDROP EDGE (firmware 1.4.0); the published
// interoperable protocol facts for Robin and Pudding
// (https://github.com/silverpoetry/HyperEars, docs/moondrop-robin-protocol.md and
// docs/moondrop-pudding-protocol.md); and the product catalog the official
// MOONDROP app fetches at runtime (analysis and the full cross-check table are
// in docs/PROTOCOL.md, "official app catalog").  Only the EDGE and NEKOCAKE
// profiles are verified on hardware by this project; catalog rows only tell us
// which module the app uses, how many batteries a model has and how many EQ
// bands it exposes.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Moondrop {

// How a device reports its battery charge.
enum class BatteryKind {
    Unknown, // no information yet
    Single, // one value for the whole headset (e.g. headphones)
    Buds, // one value per ear, 0/0xFF mean "not readable"
    BudsAndCase, // plus a case value, 0..100 is valid, 0xFF means unreadable
};

// How the three output gain steps are numbered.
enum class GainOrder {
    Unknown,
    Ascending, // 0 = low, 1 = medium, 2 = high
    Descending, // 0 = high, 1 = medium, 2 = low (MOONDROP firmware)
};

// Which module of the official MOONDROP app drives this model.  The app's
// product catalog files every Bluetooth product under exactly one of these, so
// the module is a fact about the model (not a guess); what the module's
// commands look like is only measured for EDGE so far.
enum class DeviceFamily {
    Unknown,
    Bluetrum, // BT8922E and relatives; GAIA compatible SPP (measured on EDGE)
    Qualcomm, // QCC parts, GAIA
    JieLi, // JieLi parts, SPP
    Airoha, // Airoha parts, SPP
};

struct DeviceProfile
{
    QString id; // stable identifier, e.g. "edge"
    QString name; // human readable, e.g. "MOONDROP EDGE"
    // Upper case substrings matched against the model name.  Use these for names
    // that cannot be confused with another maker (they contain the brand or are
    // otherwise distinctive); a too generic substring would make a foreign device
    // look like this model and receive its commands.
    QStringList namePatterns;
    // Full names that are accepted as well, compared case insensitively against
    // the whole model name.  For short/bare names such as "EDGE", which would be
    // far too generic as a substring.
    QStringList exactNames;
    bool isBuds = false;
    BatteryKind battery = BatteryKind::Unknown;
    QList<int> batteryIds; // ids to request, used when the device does not list them
    int ancPath = -1; // preferred command family, -1 = detect from the feature list
    bool parametricEq = false; // exposes the user EQ measured on EDGE
    GainOrder gainOrder = GainOrder::Unknown;
    bool verified = false; // measured on real hardware by this project
    QString notes;

    // ---- data from the official app's product catalog ---------------------
    // The app groups every product under one command module; this is that
    // module.  Only EDGE's command family was measured by this project.
    DeviceFamily family = DeviceFamily::Unknown;
    // user EQ band count the catalog lists for the model (0 = catalog silent).
    // The device's own answer (EQ/4) wins whenever it answers.
    int eqBands = 0;
    // gain / Q limits the catalog lists.  Defaults are what EDGE accepts.
    double gainMin = -12.0;
    double gainMax = 3.0;
    double qMin = 0.1;
    double qMax = 10.0;
    // Catalog-derived profiles are used for automatic matching only; the
    // settings page keeps offering just the measured and documented ones.
    bool selectable = true;
};

// All known profiles, most specific first; the last entry is the generic fallback.
const QList<DeviceProfile> &deviceProfiles();

// Name to show in the UI.  Product names are used as they are, the placeholders
// for unknown devices are translated.
QString profileDisplayName(const DeviceProfile &profile);

// The profile's "where do these numbers come from" note, translated.
QString profileNotes(const DeviceProfile &profile);

// Pick a profile from a model name (as reported by BASIC/GET_VARIANT, or the
// Bluetooth name).  Never returns null: unknown devices get the generic profile.
const DeviceProfile &matchProfile(const QString &modelName);

// Battery helpers -----------------------------------------------------------

// true when a reported level byte is a real percentage for this kind of device
bool batteryLevelIsValid(BatteryKind kind, int batteryId, int level);

// True when this profile was matched by comparing the whole name (exactNames
// rather than namePatterns).  Used by the tests.
bool matchesProfile(const DeviceProfile &profile, const QString &modelName);

// true when this battery id is the charging case
bool isCaseBattery(int batteryId);

} // namespace Moondrop
