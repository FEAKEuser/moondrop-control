// SPDX-License-Identifier: MIT
// Development helper: renders a single QML file (for example one of the applet
// pages) into a PNG without a Plasma shell, so the UI can be inspected without
// touching the running desktop session.
//
//   QT_QPA_PLATFORM=offscreen ./build/cli/preview package/contents/ui/AncPage.qml /tmp/anc.png 430 660
//
// Environment:
//   MOONDROP_PREVIEW_CONNECT=1   connect to the configured headphone first and
//                                show real values
//   MOONDROP_PREVIEW_FAKE=edge   run against a fake headset instead of hardware
//                                (edge, pudding or robin)
//   MOONDROP_PREVIEW_NOCONNECT=1 keep the device disconnected
//   MOONDROP_LOCALE_DIR=<dir>    translation catalogue directory
//                                (default: package/contents/locale)
//   MOONDROP_DEBUG=1             print the protocol traffic
#include "fakeheadset.h"
#include "devicediscovery.h"
#include "moondropdevice.h"
#include "i18n.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqml.h>

#include <cstdio>

// Translates through the applet's gettext catalogue (KI18n can read .mo files,
// Qt's own QTranslator cannot).
class Translator : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    Q_INVOKABLE QString translate(const QString &text) const
    {
        const QByteArray utf8 = text.toUtf8();
        return i18n(utf8.constData());
    }
};

// Prints the backend's log messages and state changes.  The connection has to go
// through the meta-object system (string based), because the backend classes live
// inside the plugin shared object.
class DebugPrinter : public QObject
{
    Q_OBJECT
public:
    explicit DebugPrinter(QObject *device, QObject *parent = nullptr)
        : QObject(parent)
        , m_device(device)
    {
        QObject::connect(m_device, SIGNAL(logMessage(QString)), this, SLOT(onLog(QString)));
        QObject::connect(m_device, SIGNAL(stateChanged()), this, SLOT(onState()));
    }

    void setEnabled(bool enabled) { m_enabled = enabled; }

public Q_SLOTS:
    void onLog(const QString &message)
    {
        if (m_enabled) {
            std::fprintf(stderr, "[device] %s\n", qPrintable(message));
        }
    }

    void onState()
    {
        if (m_enabled) {
            std::fprintf(stderr, "[state] %s\n", qPrintable(m_device->property("statusText").toString()));
        }
    }

private:
    QObject *m_device;
    bool m_enabled = false;
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QStringList args = app.arguments();
    args.removeFirst();

    const QString page = args.value(0);
    const QString output = args.value(1, QStringLiteral("/tmp/preview.png"));
    const int width = args.value(2, QStringLiteral("430")).toInt();
    const int height = args.value(3, QStringLiteral("640")).toInt();
    if (page.isEmpty()) {
        std::fprintf(stderr, "usage: preview <page.qml> [out.png] [width] [height]\n");
        return 2;
    }

    const bool connectDevice = qEnvironmentVariableIsSet("MOONDROP_PREVIEW_CONNECT")
                               && !qEnvironmentVariableIsSet("MOONDROP_PREVIEW_NOCONNECT");

    QQmlEngine engine;

    // The backend comes from the QML plugin (org.moondrop.backend), exactly like
    // in Plasma.  Prefer the build tree so a freshly built plugin is used even
    // when an older one is installed system wide.
    const QString buildImportPath = qEnvironmentVariable(
        "MOONDROP_QML_IMPORT_PATH", QDir::current().filePath(QStringLiteral("build")));
    if (QDir(buildImportPath).exists()) {
        engine.addImportPath(buildImportPath);
    }

    // Plasma injects i18n() into the QML context; reproduce it here so the pages
    // can be looked at in another language without installing the applet.
    const QString localeDir = qEnvironmentVariable(
        "MOONDROP_LOCALE_DIR", QDir::current().filePath(QStringLiteral("package/contents/locale")));
    if (QDir(localeDir).exists()) {
        KLocalizedString::addDomainLocaleDir(Moondrop::TranslationDomain, localeDir);
    }
    KLocalizedString::setApplicationDomain(Moondrop::TranslationDomain);

    Translator translator;
    engine.rootContext()->setContextProperty(QStringLiteral("__translator"), &translator);
    engine.globalObject().setProperty(
        QStringLiteral("i18n"),
        engine.evaluate(QStringLiteral("(function(text) {"
                                       "  text = __translator.translate(text);"
                                       "  for (var i = 1; i < arguments.length; ++i) {"
                                       "    text = text.replace('%' + i, arguments[i]);"
                                       "  }"
                                       "  return text;"
                                       "})")));

    // stub for the Plasma global that the pages reference
    // FullRepresentation.qml refers to `root` (the PlasmoidItem in main.qml), so
    // give it a small stand-in when rendering that file on its own.
    engine.globalObject().setProperty(
        QStringLiteral("root"),
        engine.evaluate(QStringLiteral("({ expanded: true, onDesktop: true, waitingForDevice: false,"
                                       "   connected: false, battery: -1 })")));
    engine.globalObject().setProperty(
        QStringLiteral("Plasmoid"),
        engine.evaluate(QStringLiteral("({ configuration: { showBattery: true, gainReversed: true },"
                                       "   expanded: false, formFactor: 0,"
                                       "   toolTipMainText: '', toolTipSubText: '' })")));

    // MOONDROP_PREVIEW_FAKE=<edge|pudding|robin> runs the UI against a fake
    // headset: register our own module with a device backed by a fake transport.
    // This keeps the UI (and the profile logic) workable without hardware, and
    // without taking the single RFCOMM link away from a running widget.
    const QString fake = qEnvironmentVariable("MOONDROP_PREVIEW_FAKE");
    if (!fake.isEmpty()) {
        // the fake device must never end up in the user's configuration
        qputenv("MOONDROP_CONFIG", QDir::temp().filePath(QStringLiteral("moondrop-preview-fake.ini")).toLocal8Bit());
        Moondrop::FakeHeadset::Model model = Moondrop::FakeHeadset::Edge;
        if (fake.compare(QLatin1String("pudding"), Qt::CaseInsensitive) == 0) {
            model = Moondrop::FakeHeadset::Pudding;
        } else if (fake.compare(QLatin1String("robin"), Qt::CaseInsensitive) == 0) {
            model = Moondrop::FakeHeadset::Robin;
        }
        // the plugin registers org.moondrop.backend itself, so use a private uri
        // and point the pages at it via MOONDROP_BACKEND_URI
        const char *uri = "org.moondrop.preview";
        qmlRegisterType<Moondrop::DeviceDiscovery>(uri, 1, 0, "DeviceDiscovery");
        qmlRegisterSingletonType<Moondrop::MoondropDevice>(
            uri, 1, 0, "Moondrop",
            [model](QQmlEngine *, QJSEngine *) -> QObject * {
                return new Moondrop::MoondropDevice(nullptr, new Moondrop::FakeHeadset(model));
            });
    }

    // Obtain the singleton the way QML would: by importing the module.  It is
    // only handled as a plain QObject here - the class lives inside the plugin
    // shared object, so casting to the C++ type across that boundary is not
    // possible (and not needed).
    QQmlComponent accessor(&engine);
    accessor.setData(fake.isEmpty()
                         ? "import QtQml\nimport org.moondrop.backend 1.0\n"
                           "QtObject { property var instance: Moondrop }"
                         : "import QtQml\nimport org.moondrop.preview 1.0\n"
                           "QtObject { property var instance: Moondrop }",
                     QUrl(QStringLiteral("qrc:/preview-accessor.qml")));
    QObject *accessorObject = accessor.create();
    QObject *device = accessorObject ? accessorObject->property("instance").value<QObject *>() : nullptr;
    if (!device) {
        for (const QQmlError &error : accessor.errors()) {
            std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
        }
        std::fprintf(stderr, "could not obtain the Moondrop singleton (is org.moondrop.backend importable?)\n");
        delete accessorObject;
        return 1;
    }
    // The singleton belongs to the engine and the accessor object to the JS
    // garbage collector; pin both to this process so they outlive the timers.
    QQmlEngine::setObjectOwnership(device, QQmlEngine::CppOwnership);
    device->setParent(&app);

    DebugPrinter debugPrinter(device);
    debugPrinter.setEnabled(qEnvironmentVariableIsSet("MOONDROP_DEBUG"));

    // The page normally imports the installed plugin (org.moondrop.backend); in
    // fake mode it has to import the module registered above instead.
    QFile pageFile(page);
    if (!pageFile.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "could not read %s\n", qPrintable(page));
        return 1;
    }
    QString pageSource = QString::fromUtf8(pageFile.readAll());
    if (!fake.isEmpty()) {
        pageSource.replace(QLatin1String("org.moondrop.backend"), QLatin1String("org.moondrop.preview"));
    }

    QQmlComponent pageComponent(&engine);
    pageComponent.setData(pageSource.toUtf8(), QUrl::fromLocalFile(page));
    if (pageComponent.isError()) {
        for (const QQmlError &error : pageComponent.errors()) {
            std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
        }
        return 1;
    }

    QQmlComponent component(&engine);
    const QString windowQml = QStringLiteral(R"(
        import QtQuick
        import QtQuick.Controls

        Window {
            id: previewWindow
            width: %1
            height: %2
            visible: true
            color: "#1b1e20"

            property var pageComponent: null

            Loader {
                id: loader
                anchors.fill: parent
                anchors.margins: 8
                sourceComponent: previewWindow.pageComponent
                asynchronous: false
                onStatusChanged: {
                    if (status === Loader.Error) {
                        console.error("could not load the page")
                    }
                }
            }
        }
    )")
                                  .arg(width)
                                  .arg(height);

    component.setData(windowQml.toUtf8(), QUrl(QStringLiteral("qrc:/preview.qml")));
    if (component.isError()) {
        for (const QQmlError &error : component.errors()) {
            std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
        }
        return 1;
    }

    QObject *root = component.create();
    if (root) {
        root->setProperty("pageComponent", QVariant::fromValue<QObject *>(&pageComponent));
    }
    auto *window = qobject_cast<QQuickWindow *>(root);
    if (!window) {
        std::fprintf(stderr, "could not create the preview window\n");
        delete root;
        return 1;
    }

    int status = 1;
    const auto grab = [&] {
        if (qEnvironmentVariableIsSet("MOONDROP_DEBUG")) {
            std::fprintf(stderr, "[device] connected=%d model=%s fw=%s anc=%d bands=%d\n",
                         device->property("connected").toBool(),
                         qPrintable(device->property("model").toString()),
                         qPrintable(device->property("firmwareVersion").toString()),
                         device->property("ancMode").toInt(),
                         int(device->property("bands").toList().size()));
        }
        const QImage frame = window->grabWindow();
        if (frame.isNull() || frame.width() == 0) {
            std::fprintf(stderr, "grabWindow() returned no image\n");
        } else if (!frame.save(output)) {
            std::fprintf(stderr, "could not write %s\n", qPrintable(output));
        } else {
            std::printf("wrote %s (%dx%d)\n", qPrintable(output), frame.width(), frame.height());
            status = 0;
        }
    };

    if (!fake.isEmpty()) {
        // the fake listens on channel 1 and needs some address configured
        device->setProperty("address", QStringLiteral("00:11:22:33:44:55"));
        device->setProperty("channel", 1);
    }
    if (connectDevice || !fake.isEmpty()) {
        QMetaObject::invokeMethod(device, "connectDevice");
    }
    // grab while the event loop is still running: grabbing after exec() returns
    // can produce an empty frame with some platform plugins
    const int settleMs = !fake.isEmpty() ? 2500 : (connectDevice ? 9000 : 1200);
    QTimer::singleShot(settleMs, &app, [&] {
        grab();
        app.quit();
    });

    app.exec();
    delete root;
    return status;
}

#include "preview.moc"
