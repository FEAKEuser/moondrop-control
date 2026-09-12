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

    // a name that merely contains a profile name must not match it
    expect("Samsung EDGE 5G", "unknown");

    std::printf(failures == 0 ? "\nall checks passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
