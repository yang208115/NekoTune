#include "ipc_client.h"
#include "i18n.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("NekoTune"));
    QGuiApplication::setOrganizationName(QStringLiteral("NekoTune"));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    IpcClient ipcClient;
    I18n i18n;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("ipcClient"), &ipcClient);
    engine.rootContext()->setContextProperty(QStringLiteral("i18n"), &i18n);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}
