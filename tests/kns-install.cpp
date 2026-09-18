// Programmatic KNewStuff install test.
//
// This is what "Get New Widgets" does when the user presses Install: talk OCS to
// the provider, resolve the download link, fetch the archive, unpack it through
// the KPackage structure and install it.  Driving that from a program rather than
// through the GUI dialog is the only way to run it unattended, and it exercises
// the real client code instead of a re-implementation of it - which is the point,
// because the shipped `.plasmoid` is only useful if *this* path accepts it.
//
//   ./moondrop-kns-check <knsrc-name> <scratch-xdg-data-home>
//
// The knsrc file has to be reachable through the normal search path, so the caller
// puts it in <scratch-xdg-data-home>/knsrcfiles/ and points XDG_DATA_HOME there.
// The install lands in <scratch-xdg-data-home>/plasma/plasmoids/, which the test
// then inspects - the user's own widgets are never touched.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTimer>

#include <KNSCore/EngineBase>
#include <KNSCore/Entry>
#include <KNSCore/ErrorCode>
#include <KNSCore/Provider>
#include <KNSCore/ResultsStream>
#include <KNSCore/SearchRequest>
#include <KNSCore/Transaction>

#include <cstdio>

static int failures = 0;

static void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <knsrc-name> <xdg-data-home>\n", argv[0]);
        return 2;
    }
    const QString knsrcName = QString::fromLocal8Bit(argv[1]);
    const QString dataHome = QString::fromLocal8Bit(argv[2]);
    const QString installRoot = dataHome + "/plasma/plasmoids";

    KNSCore::EngineBase *engine = new KNSCore::EngineBase(&app);

    // The whole sequence is asynchronous, so a hard timeout keeps a hung provider
    // from blocking the test for ever.
    QTimer::singleShot(90000, &app, [] {
        std::printf("FAIL timed out before the install finished\n");
        QCoreApplication::exit(1);
    });

    QObject::connect(engine, &KNSCore::EngineBase::signalErrorCode,
                     [](KNSCore::ErrorCode::ErrorCode code, const QString &message, const QVariant &) {
                         std::printf("FAIL engine error %d: %s\n", int(code), qPrintable(message));
                         ++failures;
                     });

    QObject::connect(engine, &KNSCore::EngineBase::signalProvidersLoaded, [&] {
        check(true, QStringLiteral("providers loaded from ") + knsrcName);

        // Search exactly like the dialog: no filters, the default sort.
        const KNSCore::SearchRequest request(KNSCore::SortMode::Downloads,
                                             KNSCore::Filter::None,
                                             QString(),
                                             QStringList(),
                                             0,   // first page
                                             10); // page size
        KNSCore::ResultsStream *stream = engine->search(request);
        if (!stream) {
            check(false, QStringLiteral("engine->search() returned a stream"));
            QCoreApplication::exit(1);
            return;
        }

        QObject::connect(stream, &KNSCore::ResultsStream::entriesFound, [&, stream](const KNSCore::Entry::List &entries) {
            check(!entries.isEmpty(), QStringLiteral("the provider returned at least one entry"));
            if (entries.isEmpty()) {
                return;
            }
            const KNSCore::Entry entry = entries.first();
            check(entry.name() == QLatin1String("Moondrop Control"),
                  QStringLiteral("entry is the applet: ") + entry.name());
            check(!entry.downloadLinkInformationList().isEmpty(),
                  QStringLiteral("entry carries a download link"));

            // The real install, on the real code path the dialog would use.
            KNSCore::Transaction *tx = KNSCore::Transaction::installLatest(engine, entry);
            QObject::connect(tx, &KNSCore::Transaction::signalErrorCode,
                             [](KNSCore::ErrorCode::ErrorCode code, const QString &message, const QVariant &) {
                                 std::printf("FAIL install error %d: %s\n", int(code), qPrintable(message));
                                 ++failures;
                             });
            QObject::connect(tx, &KNSCore::Transaction::finished, [&, tx] {
                check(tx->isFinished(), QStringLiteral("install transaction finished"));

                // What KNS actually put on disk has to be a working applet: the
                // plugin has to be inside the package, since the pages import it
                // through a relative directory import.
                const QString installed = installRoot + "/org.moondrop.control";
                check(QDir(installed).exists(), QStringLiteral("installed into ") + installed);
                check(QFile::exists(installed + "/metadata.json"),
                      QStringLiteral("metadata.json is present"));
                check(QFile::exists(installed + "/contents/ui/main.qml"),
                      QStringLiteral("main.qml is present"));
                check(QFile::exists(installed + "/contents/ui/backend/libmoondropplugin.so"),
                      QStringLiteral("the bundled backend plugin was unpacked from the archive"));
                check(QFile::exists(installed + "/contents/ui/backend/qmldir"),
                      QStringLiteral("the bundled qmldir is present"));
                check(QFile::exists(installed + "/contents/locale/zh_CN/LC_MESSAGES/plasma_applet_org.moondrop.control.mo"),
                      QStringLiteral("the translation catalogue came along"));

                QCoreApplication::exit(failures == 0 ? 0 : 1);
            });
        });

        QObject::connect(stream, &KNSCore::ResultsStream::finished, [] {
            std::printf("note: result stream finished\n");
        });

        // A stream is lazy: it only talks to the provider once fetch() is called.
        stream->fetch();
    });

    if (!engine->init(knsrcName)) {
        std::printf("FAIL engine->init(%s) failed - is the knsrc file on the search path?\n",
                    qPrintable(knsrcName));
        return 1;
    }
    return app.exec();
}
