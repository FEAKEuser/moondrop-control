// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostic: report every BlueZ headphone event the applet reacts to.  Used to
// confirm that a *different* headphone connecting is noticed (which is what makes
// automatic switching work), and that a disconnect is seen.
//
//   ./build/cli/moondrop-hpwatch [address-to-watch] [seconds]
#include "devicediscovery.h"
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const QString watched = args.value(1);
    const int seconds = args.value(2, QStringLiteral("30")).toInt();

    Moondrop::BlueZWatcher watcher;
    QObject::connect(&watcher, &Moondrop::BlueZWatcher::headphoneAppeared, [](const QString &a) {
        std::printf("headphoneAppeared  %s\n", qPrintable(a));
    });
    QObject::connect(&watcher, &Moondrop::BlueZWatcher::deviceConnected, [](const QString &a) {
        std::printf("deviceConnected    %s\n", qPrintable(a));
    });
    QObject::connect(&watcher, &Moondrop::BlueZWatcher::deviceDisconnected, [](const QString &a) {
        std::printf("deviceDisconnected %s\n", qPrintable(a));
    });

    watcher.watchAllHeadsets(true);
    if (!watched.isEmpty()) {
        watcher.watch(watched);
    }
    std::printf("listening for %d s (paired devices: %s)\n", seconds,
                watched.isEmpty() ? "none watched" : qPrintable(watched));
    QTimer::singleShot(seconds * 1000, &app, [] { QCoreApplication::quit(); });
    return app.exec();
}
