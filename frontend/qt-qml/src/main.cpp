#include "ipc_client.h"
#include "i18n.h"

#include <QGuiApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("NekoTune"));
    QGuiApplication::setOrganizationName(QStringLiteral("NekoTune"));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QCommandLineParser commandLine;
    commandLine.setApplicationDescription(QStringLiteral("NekoTune Qt/QML client"));
    commandLine.addHelpOption();
    commandLine.addOption(QCommandLineOption(
        {QStringLiteral("lyrics-debug"), QStringLiteral("debug-lyrics")},
        QStringLiteral("Show the lyrics timing diagnostics panel.")));
    commandLine.process(app);

    IpcClient ipcClient;
    I18n i18n;

    QQmlApplicationEngine engine;
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
