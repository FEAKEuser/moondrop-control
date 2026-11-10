// SPDX-License-Identifier: GPL-3.0-or-later
// The setters the settings page calls from QML, exercised through a real QML
// engine (no Bluetooth, fake headset):
//
//   ./build/cli/moondrop-settings-api-check
//
// The settings page calls them as functions (`Moondrop.setChannel(1)`), not as
// property assignments.  A setter that is only a Q_PROPERTY WRITE is not part of
// the meta-object's method table, so those calls fail with "is not a function"
// and the setting silently keeps its old value.  Nothing in C++ catches that -
// only a QML engine does, which is why this check exists.
#include "fakeheadset.h"
#include "moondropdevice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>

#include <cstdio>
#include <functional>

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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // never touch the user's configuration
    const QString configPath = QDir::temp().filePath(QStringLiteral("moondrop-settings-api.ini"));
    QFile::remove(configPath);
    qputenv("MOONDROP_CONFIG", configPath.toLocal8Bit());

    // owned by the QML engine once registered (it deletes it on shutdown)
    auto *device = new MoondropDevice(nullptr, new FakeHeadset(FakeHeadset::Edge));
    device->disableStartupAutoConnect();

    const char *uri = "org.moondrop.apicheck";
    qmlRegisterSingletonType<MoondropDevice>(uri, 1, 0, "Moondrop",
                                             [device](QQmlEngine *, QJSEngine *) -> QObject * {
                                                 return device;
                                             });

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQml\nimport org.moondrop.apicheck 1.0\nQtObject {\n"
                      "}\n",
                      QUrl(QStringLiteral("qrc:/settings-api-check.qml")));
    QObject *scope = component.create();
    if (!scope) {
        std::printf("FAIL could not create the QML scope object: %s\n",
                    qPrintable(component.errorString()));
        std::printf("\nFAILURES\n");
        return 1;
    }

    // evaluate one expression like QML would, reporting its runtime error
    auto run = [&](const QString &expression) {
        QQmlExpression e(QQmlEngine::contextForObject(scope), scope, expression);
        e.evaluate();
        if (e.hasError()) {
            std::printf("     %s\n", qPrintable(e.error().toString()));
            e.clearError();
            return false;
        }
        return true;
    };

    device->setAddress(QStringLiteral("00:11:22:33:44:55"));

    check(run(QStringLiteral("Moondrop.setChannel(7)")),
          QStringLiteral("setChannel is callable from QML"));
    check(device->channel() == 7, QStringLiteral("setChannel took effect"));

    check(run(QStringLiteral("Moondrop.setAutoConnect(false)")),
          QStringLiteral("setAutoConnect is callable from QML"));
    check(!device->autoConnect(), QStringLiteral("setAutoConnect took effect"));

    check(run(QStringLiteral("Moondrop.setAutoReconnect(false)")),
          QStringLiteral("setAutoReconnect is callable from QML"));
    check(!device->autoReconnect(), QStringLiteral("setAutoReconnect took effect"));

    check(run(QStringLiteral("Moondrop.setAncRestoreLast(true)")),
          QStringLiteral("setAncRestoreLast is callable from QML"));
    check(device->ancRestoreLast(), QStringLiteral("setAncRestoreLast took effect"));

    check(run(QStringLiteral("Moondrop.setAddress('00:11:22:33:44:66')")),
          QStringLiteral("setAddress is callable from QML"));
    check(device->address() == QLatin1String("00:11:22:33:44:66"),
          QStringLiteral("setAddress took effect"));

    check(run(QStringLiteral("Moondrop.ancRestoreLast = false")),
          QStringLiteral("the property assignment form still works"));
    check(!device->ancRestoreLast(), QStringLiteral("the property assignment took effect"));

    delete scope;
    QFile::remove(configPath);

    std::printf("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
