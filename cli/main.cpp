// SPDX-License-Identifier: GPL-3.0-or-later
// Command line front end for the MOONDROP backend.  Handy for testing and for
// scripting: moondrop-cli anc on && moondrop-cli preset 0x3f
#include "devicediscovery.h"
#include "fakeheadset.h"
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QDir>
#include <QTextStream>
#include <QTimer>

using namespace Moondrop;

namespace {

QTextStream out(stdout);

QString ancName(int mode)
{
    switch (mode) {
    case AncOff: return QStringLiteral("off");
    case AncNoiseCancelling: return QStringLiteral("anc");
    case AncTransparency: return QStringLiteral("transparency");
    case AncWind: return QStringLiteral("wind");
    case AncAdaptive: return QStringLiteral("adaptive");
    case AncLive: return QStringLiteral("live");
    default: return QStringLiteral("unknown");
    }
}

void printBands(const QVariantList &bands)
{
    out << "  band   frequency      Q       gain   type\n";
    int index = 1;
    for (const QVariant &item : bands) {
        const QVariantMap band = item.toMap();
        out << QStringLiteral("  %1      %2 Hz    %3   %4 dB  %5\n")
                   .arg(index++, 2)
                   .arg(band.value(QStringLiteral("frequency")).toInt(), 5)
                   .arg(band.value(QStringLiteral("q")).toDouble(), 5, 'f', 2)
                   .arg(band.value(QStringLiteral("gain")).toDouble(), 6, 'f', 1)
                   .arg(band.value(QStringLiteral("type")).toInt());
    }
}

void printInfo(MoondropDevice *device)
{
    out << "state      : " << device->statusText() << "  (" << device->address() << ")\n";
    out << "model      : " << device->model() << "\n";
    out << "firmware   : " << device->firmwareVersion() << "   GAIA " << device->gaiaVersion() << "\n";
    out << "serial     : " << device->serialNumber() << "\n";
    out << "host BT    : " << device->hostAddress() << "\n";
    out << "battery    : " << device->batteryLevel() << " %";
    for (const QVariant &item : device->batteries()) {
        const QVariantMap entry = item.toMap();
        out << QStringLiteral("   [id %1: %2 %]")
                   .arg(entry.value(QStringLiteral("id")).toInt())
                   .arg(entry.value(QStringLiteral("level")).toInt());
    }
    out << "\n";
    out << "ANC        : feature " << device->ancPath() << ", mode " << ancName(device->ancMode()) << "\n";
    out << "EQ         : " << (device->eqSupported() ? "supported" : "not detected") << "\n";
    if (!device->presetIds().isEmpty()) {
        for (int i = 0; i < device->presetIds().size(); ++i) {
            const int id = device->presetIds().at(i).toInt();
            out << QStringLiteral("   %1 id 0x%2  %3\n")
                       .arg(id == device->currentPreset() ? QStringLiteral("*") : QStringLiteral(" "))
                       .arg(id, 2, 16, QLatin1Char('0'))
                       .arg(device->presetName(id));
        }
    }
    if (!device->bands().isEmpty()) {
        out << "user PEQ:\n";
        printBands(device->bands());
    }
    out << "LDAC       : " << (device->ldacSupported() ? (device->ldacEnabled() ? "on" : "off") : "unsupported") << "\n";
    out << "LC3 / LHDC : " << (device->lc3Supported() ? "yes" : "no") << " / "
        << (device->lhdcSupported() ? "yes" : "no") << "\n";
    out << "DAC gain   : " << (device->dacGainSupported() ? QString::number(device->dacGain()) : QStringLiteral("unsupported")) << "\n";
    out << "multipoint : " << (device->multipointSupported() ? (device->multipointEnabled() ? "on" : "off") : "unsupported") << "\n";
    if (!device->features().isEmpty()) {
        QStringList names;
        for (const QVariant &item : device->features()) {
            names << item.toMap().value(QStringLiteral("name")).toString();
        }
        out << "features   : " << names.join(QStringLiteral(", ")) << "\n";
    }
    out.flush();
}

int usage()
{
    out << "usage: moondrop-cli [--address XX:XX:..] [--channel N] <command>\n\n"
        << "  list                              Bluetooth devices known to BlueZ\n"
        << "  info                              everything the headphone reports\n"
        << "  anc [off|on|transparency|wind]    get or set noise cancelling\n"
        << "  preset list | preset ID           list / select an EQ preset (0x3f = PEQ)\n"
        << "  peq get | peq flat | peq restore   show / flatten / restore the user PEQ\n"
        << "  peq set \"f:g:q;f:g:q;...\"          write bands (as many as the model has)\n"
        << "  battery                           battery levels\n"
        << "  ldac [on|off]\n"
        << "  extra list | extra ID [VALUE]    extra settings (led, timeouts, ...)\n"
        << "  anc-switch [STATE ON OFF TP ORDER]  ANC switch configuration\n"
        << "  mp devices | mp next | mp timeout [N] | mp disconnect N\n"
        << "  gesture list | gesture reset | gesture set GID TOUCHPAD CONTEXT ACTION\n"
        << "  gain [0|1|2]                      DAC gain\n"
        << "  raw FEATURE CMD [HEXPAYLOAD]      arbitrary GAIA command\n"
        << "  monitor [seconds]                 just print notifications\n";
    out.flush();
    return 2;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("moondrop-widget"));
    QCoreApplication::setApplicationName(QStringLiteral("moondrop-cli"));

    QStringList args = app.arguments();
    args.removeFirst();

    QString address;
    int channel = 0;
    while (args.size() >= 2 && args.first().startsWith(QLatin1String("--"))) {
        const QString option = args.takeFirst();
        const QString value = args.takeFirst();
        if (option == QLatin1String("--address") || option == QLatin1String("--addr")) {
            address = value;
        } else if (option == QLatin1String("--channel")) {
            channel = value.toInt();
        } else {
            return usage();
        }
    }

    const QString command = args.isEmpty() ? QStringLiteral("info") : args.takeFirst();

    if (command == QLatin1String("help") || command == QLatin1String("-h") || command == QLatin1String("--help")) {
        return usage();
    }

    if (command == QLatin1String("list")) {
        DeviceDiscovery discovery;
        discovery.refresh();
        if (!discovery.error().isEmpty()) {
            out << "error: " << discovery.error() << "\n";
            out.flush();
            return 1;
        }
        for (const QVariant &item : discovery.devices()) {
            const QVariantMap entry = item.toMap();
            out << QStringLiteral("%1  %2%3%4\n")
                       .arg(entry.value(QStringLiteral("address")).toString(), -20)
                       .arg(entry.value(QStringLiteral("name")).toString(), -28)
                       .arg(entry.value(QStringLiteral("paired")).toBool() ? QStringLiteral("paired ") : QStringLiteral("       "))
                       .arg(entry.value(QStringLiteral("connected")).toBool() ? QStringLiteral("connected") : QString());
        }
        out.flush();
        return 0;
    }

    // MOONDROP_FAKE=<edge|pudding|robin> runs against a fake headset: the write
    // paths can then be exercised without hardware (and without taking the single
    // RFCOMM link away from a running widget).
    const QString fake = qEnvironmentVariable("MOONDROP_FAKE");
    if (!fake.isEmpty()) {
        // never write the fake device into the user's configuration
        qputenv("MOONDROP_CONFIG", QDir::temp().filePath(QStringLiteral("moondrop-cli-fake.ini")).toLocal8Bit());
    }
    MoondropDevice *devicePtr = nullptr;
    if (!fake.isEmpty()) {
        Moondrop::FakeHeadset::Model model = Moondrop::FakeHeadset::Edge;
        if (fake.compare(QLatin1String("pudding"), Qt::CaseInsensitive) == 0) {
            model = Moondrop::FakeHeadset::Pudding;
        } else if (fake.compare(QLatin1String("robin"), Qt::CaseInsensitive) == 0) {
            model = Moondrop::FakeHeadset::Robin;
        } else if (fake.compare(QLatin1String("rich"), Qt::CaseInsensitive) == 0) {
            model = Moondrop::FakeHeadset::Rich;
        }
        devicePtr = new MoondropDevice(nullptr, new Moondrop::FakeHeadset(model));
    } else {
        devicePtr = new MoondropDevice();
    }
    MoondropDevice &device = *devicePtr;
    // this tool decides itself when to connect
    device.disableStartupAutoConnect();
    if (!address.isEmpty()) {
        device.setAddress(address);
    }
    if (channel > 0) {
        device.setChannel(channel);
    }
    if (!fake.isEmpty()) {
        device.setAddress(QStringLiteral("00:11:22:33:44:55"));
        device.setChannel(1);
    } else if (device.address().isEmpty()) {
        out << "No headphone configured yet. Use --address, or pick one in the widget.\n";
        out.flush();
        return 1;
    }

    const bool monitor = (command == QLatin1String("monitor"));
    const bool verbose = qEnvironmentVariableIsSet("MOONDROP_DEBUG");

    QObject::connect(&device, &MoondropDevice::logMessage, [&](const QString &message) {
        // the state/error connections above already print in the simple case
        if (verbose || monitor) {
            out << message << "\n";
            out.flush();
        }
    });
    QObject::connect(&device, &MoondropDevice::stateChanged, [&] {
        if (verbose) {
            out << "[state] " << device.statusText() << "\n";
            out.flush();
        }
    });
    // errors are always reported, they are the interesting part of a failure
    QObject::connect(&device, &MoondropDevice::lastErrorChanged, [&] {
        if (!device.lastError().isEmpty() && !verbose) {
            out << "error: " << device.lastError() << "\n";
            out.flush();
        }
    });

    int exitCode = 1;
    bool started = false;

    const std::function<void()> runCommand = [&] {
        exitCode = 0;
        if (monitor) {
            const int seconds = args.isEmpty() ? 30 : args.first().toInt();
            out << "listening for " << seconds << " s ...\n";
            out.flush();
            QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
            return;
        }
        if (command == QLatin1String("info")) {
            printInfo(&device);
        } else if (command == QLatin1String("anc")) {
            if (args.isEmpty()) {
                out << ancName(device.ancMode()) << "\n";
                out.flush();
                QTimer::singleShot(0, &app, &QCoreApplication::quit);
                return;
            }
            const QString mode = args.first();
            if (mode == QLatin1String("off")) {
                device.setAncMode(AncOff);
            } else if (mode == QLatin1String("on") || mode == QLatin1String("anc")) {
                device.setAncMode(AncNoiseCancelling);
            } else if (mode == QLatin1String("transparency") || mode == QLatin1String("passthrough")) {
                device.setAncMode(AncTransparency);
            } else if (mode == QLatin1String("wind")) {
                device.setAncMode(AncWind);
            } else {
                usage();
            }
        } else if (command == QLatin1String("preset")) {
            if (args.isEmpty() || args.first() == QLatin1String("list")) {
                for (int i = 0; i < device.presetIds().size(); ++i) {
                    const int id = device.presetIds().at(i).toInt();
                    out << QStringLiteral("%1 0x%2  %3\n")
                               .arg(id == device.currentPreset() ? QStringLiteral("*") : QStringLiteral(" "),
                                    QString::number(id, 16).rightJustified(2, QLatin1Char('0')),
                                    device.presetName(id));
                }
            } else {
                device.selectPreset(args.first().toInt(nullptr, 0));
            }
            out.flush();
        } else if (command == QLatin1String("peq")) {
            const QString sub = args.isEmpty() ? QStringLiteral("get") : args.first();
            if (sub == QLatin1String("get")) {
                printBands(device.bands());
                out.flush();
            } else if (sub == QLatin1String("restore")) {
                device.restoreDeviceEq();
            } else if (sub == QLatin1String("flat")) {
                // keep the frequencies the device reports: a ten band model has
                // ten of them.  Without a curve yet, use the measured EDGE layout.
                QVariantList bands;
                const QVariantList current = device.bands();
                if (!current.isEmpty()) {
                    for (const QVariant &item : current) {
                        QVariantMap band = item.toMap();
                        band.insert(QStringLiteral("gain"), 0.0);
                        band.insert(QStringLiteral("q"), 1.0);
                        bands.append(band);
                    }
                } else {
                    const QList<int> freqs{23, 240, 1400, 2300, 6900};
                    for (int freq : freqs) {
                        QVariantMap band;
                        band.insert(QStringLiteral("frequency"), freq);
                        band.insert(QStringLiteral("gain"), 0.0);
                        band.insert(QStringLiteral("q"), 1.0);
                        band.insert(QStringLiteral("type"), 0);
                        bands.append(band);
                    }
                }
                device.applyUserEq(bands, true);
            } else if (sub == QLatin1String("set") && args.size() >= 2) {
                const QStringList parts = args.at(1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
                if (parts.isEmpty() || parts.size() > 32) {
                    out << "need between 1 and 32 bands\n";
                    out.flush();
                    app.exit(2);
                    return;
                }
                if (!device.bands().isEmpty() && device.bands().size() != parts.size()) {
                    out << QStringLiteral("this headphone has %1 bands\n").arg(device.bands().size());
                    out.flush();
                    app.exit(2);
                    return;
                }
                QVariantList bands;
                for (const QString &part : parts) {
                    const QStringList fields = part.split(QLatin1Char(':'));
                    QVariantMap band;
                    band.insert(QStringLiteral("frequency"), fields.value(0).toInt());
                    band.insert(QStringLiteral("gain"), fields.value(1).toDouble());
                    band.insert(QStringLiteral("q"), fields.value(2, QStringLiteral("1.0")).toDouble());
                    band.insert(QStringLiteral("type"), 0);
                    bands.append(band);
                }
                device.applyUserEq(bands, true);
            } else {
                usage();
            }
        } else if (command == QLatin1String("battery")) {
            if (device.batteries().isEmpty()) {
                // models without a GAIA battery feature (NEKOCAKE) still report a
                // level through BlueZ, which the backend falls back to
                if (device.batteryLevel() >= 0) {
                    out << QStringLiteral("battery: %1 %\n").arg(device.batteryLevel());
                } else {
                    out << "no battery information\n";
                }
            }
            for (const QVariant &item : device.batteries()) {
                const QVariantMap entry = item.toMap();
                out << QStringLiteral("battery %1: %2 %\n")
                           .arg(entry.value(QStringLiteral("id")).toInt())
                           .arg(entry.value(QStringLiteral("level")).toInt());
            }
            out.flush();
        } else if (command == QLatin1String("ldac")) {
            if (args.isEmpty()) {
                out << (device.ldacEnabled() ? "on" : "off") << "\n";
                out.flush();
                QTimer::singleShot(0, &app, &QCoreApplication::quit);
                return;
            }
            device.setLdacEnabled(args.first() == QLatin1String("on"));
        } else if (command == QLatin1String("gain")) {
            if (args.isEmpty()) {
                out << device.dacGain() << "\n";
                out.flush();
                QTimer::singleShot(0, &app, &QCoreApplication::quit);
                return;
            }
            device.setDacGain(args.first().toInt());
        } else if (command == QLatin1String("extra")) {
            if (args.isEmpty() || args.first() == QLatin1String("list")) {
                for (const QVariant &item : device.extraSettings()) {
                    const QVariantMap entry = item.toMap();
                    out << QStringLiteral("%1 = %2   (%3)\n")
                               .arg(entry.value(QStringLiteral("id")).toString(), -22)
                               .arg(entry.value(QStringLiteral("value")).toString(), -6)
                               .arg(entry.value(QStringLiteral("label")).toString());
                }
                out.flush();
            } else if (args.size() == 1) {
                const QString id = args.first();
                bool found = false;
                for (const QVariant &item : device.extraSettings()) {
                    const QVariantMap entry = item.toMap();
                    if (entry.value(QStringLiteral("id")).toString() != id) {
                        continue;
                    }
                    out << QStringLiteral("%1 = %2   (%3)\n")
                               .arg(id, -22)
                               .arg(entry.value(QStringLiteral("value")).toString(), -6)
                               .arg(entry.value(QStringLiteral("label")).toString());
                    found = true;
                    break;
                }
                if (!found) {
                    out << "unknown extra setting: " << id << "\n";
                    out.flush();
                    app.exit(2);
                    return;
                }
                out.flush();
            } else if (args.size() >= 2) {
                const QString id = args.at(0);
                QVariant value = args.at(1);
                for (const QVariant &item : device.extraSettings()) {
                    const QVariantMap entry = item.toMap();
                    if (entry.value(QStringLiteral("id")).toString() != id) {
                        continue;
                    }
                    if (entry.value(QStringLiteral("kind")).toString() == QLatin1String("bool")) {
                        const QString text = value.toString().toLower();
                        value = (text == QLatin1String("1") || text == QLatin1String("on")
                                 || text == QLatin1String("true") || text == QLatin1String("yes"));
                    } else {
                        value = value.toInt();
                    }
                    break;
                }
                device.setExtra(id, value);
            } else {
                usage();
                app.exit(2);
            }
        } else if (command == QLatin1String("anc-switch")) {
            if (args.size() >= 5) {
                device.setAncSwitchConfig(args.at(0) != QLatin1String("0"),
                                          args.at(1) != QLatin1String("0"),
                                          args.at(2) != QLatin1String("0"),
                                          args.at(3) != QLatin1String("0"),
                                          args.at(4).toInt());
            } else {
                const QVariantMap config = device.ancSwitchConfig();
                out << "state        : " << (config.value(QStringLiteral("state")).toBool() ? "on" : "off") << "\n";
                out << "noise cancel : " << (config.value(QStringLiteral("ancOn")).toBool() ? "yes" : "no") << "\n";
                out << "off          : " << (config.value(QStringLiteral("ancOff")).toBool() ? "yes" : "no") << "\n";
                out << "transparency : " << (config.value(QStringLiteral("transparency")).toBool() ? "yes" : "no") << "\n";
                out << "order        : " << config.value(QStringLiteral("order")).toInt() << "\n";
                out.flush();
            }
        } else if (command == QLatin1String("mp")) {
            const QString sub = args.isEmpty() ? QStringLiteral("devices") : args.first();
            if (sub == QLatin1String("devices")) {
                for (const QVariant &item : device.multipointDevices()) {
                    const QVariantMap entry = item.toMap();
                    out << QStringLiteral("#%1  %2  %3\n")
                               .arg(entry.value(QStringLiteral("number")).toInt())
                               .arg(entry.value(QStringLiteral("address")).toString(), -20)
                               .arg(entry.value(QStringLiteral("name")).toString());
                }
                if (device.multipointTimeout() >= 0) {
                    out << "timeout: " << device.multipointTimeout() << " min\n";
                }
                out.flush();
            } else if (sub == QLatin1String("next")) {
                device.requestNextMultipointDevice();
            } else if (sub == QLatin1String("timeout") && args.size() == 1) {
                if (device.multipointTimeout() >= 0) {
                    out << "timeout: " << device.multipointTimeout() << " min\n";
                } else {
                    out << "multipoint timeout: unknown\n";
                }
                out.flush();
            } else if (sub == QLatin1String("timeout") && args.size() >= 2) {
                device.setExtra(QStringLiteral("multipointTimeout"), args.at(1).toInt());
            } else if (sub == QLatin1String("disconnect") && args.size() >= 2) {
                device.disconnectMultipointDevice(args.at(1).toInt());
            } else {
                usage();
                app.exit(2);
            }
        } else if (command == QLatin1String("gesture")) {
            const QString sub = args.isEmpty() ? QStringLiteral("list") : args.first();
            if (sub == QLatin1String("list")) {
                for (const QVariant &row : device.gestures()) {
                    const QVariantMap gesture = row.toMap();
                    out << gesture.value(QStringLiteral("label")).toString() << "\n";
                    for (const QVariant &item : gesture.value(QStringLiteral("entries")).toList()) {
                        const QVariantMap entry = item.toMap();
                        out << QStringLiteral("   %1 · %2 -> %3  (%4/%5/%6)\n")
                                   .arg(entry.value(QStringLiteral("touchpadLabel")).toString(), -14)
                                   .arg(entry.value(QStringLiteral("contextLabel")).toString(), -14)
                                   .arg(entry.value(QStringLiteral("actionLabel")).toString())
                                   .arg(entry.value(QStringLiteral("touchpad")).toInt())
                                   .arg(entry.value(QStringLiteral("context")).toInt())
                                   .arg(entry.value(QStringLiteral("action")).toInt());
                    }
                }
                out.flush();
            } else if (sub == QLatin1String("reset")) {
                device.resetGestureConfiguration();
            } else if (sub == QLatin1String("set") && args.size() >= 5) {
                device.setGestureAction(args.at(1).toInt(), args.at(2).toInt(),
                                        args.at(3).toInt(), args.at(4).toInt());
            } else {
                usage();
                app.exit(2);
            }
        } else if (command == QLatin1String("raw")) {
            if (args.size() < 2) {
                usage();
                app.exit(2);
                return;
            }
            device.sendRaw(args.at(0).toInt(nullptr, 0), args.at(1).toInt(nullptr, 0),
                           args.value(2));
        } else {
            usage();
            app.exit(2);
            return;
        }
        // the idle watcher quits the app once the device is done answering
    };

    // wait until the initial state refresh is done before doing anything
    auto idleWatch = new QTimer(&app);
    idleWatch->setInterval(100);
    QObject::connect(&device, &MoondropDevice::stateChanged, [&] {
        if (started || device.state() != MoondropDevice::Connected) {
            return;
        }
        started = true;
        idleWatch->start();
        if (monitor) {
            QTimer::singleShot(200, &app, runCommand);
        }
    });
    QObject::connect(idleWatch, &QTimer::timeout, [&] {
        if (monitor || exitCode == 0) {
            return;
        }
        if (!device.busy() && device.ready()) {
            idleWatch->stop();
            runCommand();
            auto *finishWatch = new QTimer(&app);
            finishWatch->setInterval(100);
            QObject::connect(finishWatch, &QTimer::timeout, [&, finishWatch] {
                if (!device.busy()) {
                    finishWatch->stop();
                    QTimer::singleShot(250, &app, &QCoreApplication::quit);
                }
            });
            finishWatch->start();
        }
    });

    QTimer::singleShot(0, &device, [&] {
        device.connectDevice();
    });

    QTimer::singleShot(90000, &app, [&] {
        out << "timeout: could not connect\n";
        out.flush();
        app.exit(1);
    });

    // stop early when the backend gave up (e.g. another program holds the
    // control channel) instead of waiting for the timeout above
    QObject::connect(&device, &MoondropDevice::stateChanged, [&] {
        if (!started && device.state() == MoondropDevice::Disconnected
            && !device.lastError().isEmpty()) {
            QTimer::singleShot(200, &app, [&] { app.exit(1); });
        }
    });

    const int result = app.exec();
    return started ? result : 1;
}
