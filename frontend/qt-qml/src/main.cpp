#include "controllers/app_controllers.h"
#include "i18n.h"
#include "app_paths.h"
#include "ipc_client.h"

#ifdef NEKOTUNE_EMBED_BACKEND
#include "runtime/backend_runtime.h"
#endif

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QStandardPaths>

class CoverNetworkManager final : public QNetworkAccessManager {
  public:
    using QNetworkAccessManager::QNetworkAccessManager;

  protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override {
        QNetworkRequest cachedRequest(request);
        if (operation == GetOperation &&
            request.url().host().compare(QStringLiteral("imge.kugou.com"), Qt::CaseInsensitive) == 0)
            cachedRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                                       QNetworkRequest::PreferCache);
        return QNetworkAccessManager::createRequest(operation, cachedRequest, outgoingData);
    }
};

class CoverNetworkManagerFactory final : public QQmlNetworkAccessManagerFactory {
  public:
    QNetworkAccessManager *create(QObject *parent) override {
        auto *manager = new CoverNetworkManager(parent);
        auto *cache = new QNetworkDiskCache(manager);
        const auto directory = nekotune::AppPaths::configFile("covers");
        cache->setCacheDirectory(directory);
        cache->setMaximumCacheSize(64 * 1024 * 1024);
        manager->setCache(cache);
        return manager;
    }
};

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("NekoTune"));
    QGuiApplication::setOrganizationName(QStringLiteral("NekoTune"));
    QString directoryError;
    if (!nekotune::AppPaths::prepare(&directoryError)) { qCritical() << directoryError; return 1; }
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

#ifdef NEKOTUNE_EMBED_BACKEND
    nekotune::BackendRuntime runtime;
    if (!runtime.start()) {
        qCritical() << runtime.errorString();
        return 1;
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &runtime, &nekotune::BackendRuntime::stop);
    qInfo() << "NekoTune backend listening on" << runtime.serverName();
#endif

    QCommandLineParser commandLine;
    commandLine.setApplicationDescription(QStringLiteral("NekoTune Qt/QML client"));
    commandLine.addHelpOption();
    commandLine.addOption(
        QCommandLineOption({QStringLiteral("lyrics-debug"), QStringLiteral("debug-lyrics")},
                           QStringLiteral("Enable the lyrics diagnostics page in the sidebar.")));
    commandLine.process(app);

    IpcClient ipcClient;
    I18n i18n;
    AppControllers controllers(ipcClient);

    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(new CoverNetworkManagerFactory);
    engine.rootContext()->setContextProperty(QStringLiteral("ipcClient"), &ipcClient);
    engine.rootContext()->setContextProperty(QStringLiteral("i18n"), &i18n);
    engine.rootContext()->setContextProperty(QStringLiteral("controllers"), &controllers);
    engine.rootContext()->setContextProperty(QStringLiteral("lyricsDebugEnabled"),
                                             commandLine.isSet(QStringLiteral("lyrics-debug")) ||
                                                 commandLine.isSet(QStringLiteral("debug-lyrics")));
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}
