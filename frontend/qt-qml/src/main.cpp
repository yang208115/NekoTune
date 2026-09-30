#include "ipc_client.h"
#include "i18n.h"

#ifdef NEKOTUNE_EMBED_BACKEND
#include "core/player_engine.h"
#include "ipc/ipc_server.h"
#endif

#include <QGuiApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QStandardPaths>
#include <QDir>
#include <QQuickStyle>

class CoverNetworkManager final : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override
    {
        QNetworkRequest cachedRequest(request);
        if (operation == GetOperation &&
            request.url().host().compare(QStringLiteral("imge.kugou.com"), Qt::CaseInsensitive) == 0)
            cachedRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        return QNetworkAccessManager::createRequest(operation, cachedRequest, outgoingData);
    }
};

class CoverNetworkManagerFactory final : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        auto *manager = new CoverNetworkManager(parent);
        auto *cache = new QNetworkDiskCache(manager);
        const auto directory = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                                   .filePath(QStringLiteral("covers"));
        cache->setCacheDirectory(directory);
        cache->setMaximumCacheSize(64 * 1024 * 1024);
        manager->setCache(cache);
        return manager;
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("NekoTune"));
    QGuiApplication::setOrganizationName(QStringLiteral("NekoTune"));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

#ifdef NEKOTUNE_EMBED_BACKEND
    // The unified desktop target owns the backend in the same process.  Keep
    // the IPC boundary intact so the QML client and future replaceable
    // clients exercise exactly the same API as the standalone build.
    nekotune::PlayerEngine player;
    nekotune::IpcServer server(player);
    if (!server.listen()) {
        qCritical() << "Failed to listen on" << server.serverName()
                    << server.errorString();
        return 1;
    }
    qInfo() << "NekoTune backend listening on" << server.serverName();
#endif

    QCommandLineParser commandLine;
    commandLine.setApplicationDescription(QStringLiteral("NekoTune Qt/QML client"));
    commandLine.addHelpOption();
    commandLine.addOption(QCommandLineOption(
        {QStringLiteral("lyrics-debug"), QStringLiteral("debug-lyrics")},
        QStringLiteral("Enable the lyrics diagnostics page in the sidebar.")));
    commandLine.process(app);

    IpcClient ipcClient;
    I18n i18n;

    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(new CoverNetworkManagerFactory);
    engine.rootContext()->setContextProperty(QStringLiteral("ipcClient"), &ipcClient);
    engine.rootContext()->setContextProperty(QStringLiteral("i18n"), &i18n);
    engine.rootContext()->setContextProperty(QStringLiteral("lyricsDebugEnabled"),
                                              commandLine.isSet(QStringLiteral("lyrics-debug")) ||
                                                  commandLine.isSet(QStringLiteral("debug-lyrics")));
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}
