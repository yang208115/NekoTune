#include "core/player_engine.h"
#include "ipc/ipc_server.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NekoTune Backend"));
    QCoreApplication::setOrganizationName(QStringLiteral("NekoTune"));

    nekotune::PlayerEngine player;
    nekotune::IpcServer server(player);

    if (!server.listen()) {
        qCritical() << "Failed to listen on" << server.serverName()
                    << server.errorString();
        return 1;
    }

    qInfo() << "NekoTune backend listening on" << server.serverName();
    return app.exec();
}
