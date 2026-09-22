#pragma once

#include "core/player_engine.h"
#include "ipc/ipc_router.h"

#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>

namespace nekotune {

class IpcServer final : public QObject {
    Q_OBJECT

public:
    explicit IpcServer(PlayerEngine &player, QObject *parent = nullptr);

    bool listen();
    QString serverName() const;
    QString errorString() const;

private slots:
    void acceptConnection();
    void readClient();
    void removeClient(QObject *client);
    void broadcastEvent(const QJsonObject &event);

private:
    static QString defaultServerName();

    void send(QLocalSocket *client, const QJsonObject &payload);

    PlayerEngine &m_player;
    IpcRouter m_router;
    QLocalServer m_server;
    QString m_serverName;
    QString m_error;
    QHash<QLocalSocket *, QByteArray> m_buffers;
};

} // namespace nekotune
