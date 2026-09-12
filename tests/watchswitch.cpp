// SPDX-License-Identifier: GPL-3.0-or-later
// End-to-end observation of the switch: connect whatever is available, then keep
// reporting every BlueZ event and connection attempt.  Run this while physically
// switching headphones to see whether the backend recovers with no user input.
//
//   ./build/cli/moondrop-watchswitch <seconds>
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QFile>
#include <QTime>
#include <QTimer>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const int seconds = app.arguments().value(1, QStringLiteral("60")).toInt();
    qputenv("MOONDROP_CONFIG", "/tmp/moondrop-watchswitch.ini");
    QFile::remove("/tmp/moondrop-watchswitch.ini");

    auto *device = new Moondrop::MoondropDevice();
    device->setAutoReconnect(true);
    // start from nothing selected, so autodetection picks the live headphone
    QObject::connect(device, &Moondrop::MoondropDevice::logMessage, [](const QString &m) {
        std::printf("[%s] %s\n", QTime::currentTime().toString("HH:mm:ss").toUtf8().constData(),
                    qPrintable(m));
    });
    QObject::connect(device, &Moondrop::MoondropDevice::stateChanged, [&] {
        std::printf("  -> state=%d address=%s model=%s\n", int(device->state()),
                    qPrintable(device->address()), qPrintable(device->model()));
    });

    std::printf("watching for %d s - switch headphones now\n", seconds);
    QTimer::singleShot(seconds * 1000, &app, [&] {
        std::printf("--- final: state=%d address=%s model=%s ---\n",
                    int(device->state()), qPrintable(device->address()), qPrintable(device->model()));
        QCoreApplication::quit();
    });
    return app.exec();
}
