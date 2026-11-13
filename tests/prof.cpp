// SPDX-License-Identifier: GPL-3.0-or-later
// Device-profile matching check: the measured models must be recognised, and a
// foreign name must NOT be mistaken for one (the "Samsung EDGE 5G" trap).
#include "deviceprofile.h"

#include <QCoreApplication>

#include <cstdio>

static int failures = 0;

static void expect(const char *modelName, const char *expectedId)
{
    // copy, not a reference: matchProfile() returns the element of a temporary list
    const QString id = Moondrop::matchProfile(QString::fromUtf8(modelName)).id;
    const bool ok = id == QLatin1String(expectedId);
    std::printf("%s  %-22s -> %s (expected %s)\n", ok ? "ok  " : "FAIL",
                modelName, qPrintable(id), expectedId);
    if (!ok) {
        ++failures;
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // measured models
    expect("MOONDROP EDGE", "edge");
    expect("Moondrop Nekocake", "nekocake");
    expect("MOONDROP NEKOCAKE", "nekocake");
    expect("MOONDROP Pudding", "pudding");
    expect("Robin's Earphones", "robin");
    // catalog-derived rows must win over the older substring patterns
    expect("MOONDROP EDGE 2", "edge-2");
    expect("SPACE TRAVEL 2", "space-travel-2");
    expect("MOONDROP SPACE TRAVEL 2 ULTRA", "space-travel-2-ultra");

    // the measured NEKOCAKE must not be labelled with the catalog fallback note
    {
        const Moondrop::DeviceProfile nekocake = Moondrop::matchProfile(QStringLiteral("Moondrop Nekocake"));
        const QString notes = Moondrop::profileNotes(nekocake);
        const bool catalogNote = notes.contains(QLatin1String("Official app catalog"));
        const bool measured = notes.contains(QLatin1String("1.0.0"));
        std::printf("%s  %-22s -> notes=%s\n", (measured && !catalogNote) ? "ok  " : "FAIL",
                    "NEKOCAKE provenance", qPrintable(notes));
        if (!(measured && !catalogNote)) {
            ++failures;
        }
    }

    // a name that merely contains a profile name must not match it
    expect("Samsung EDGE 5G", "unknown");
    expect("Poly Voyager Focus 2", "unknown");
    expect("Jabra Voyager", "unknown");

    std::printf(failures == 0 ? "\nall checks passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
