#include "ipc/ipc_server.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcessEnvironment>

namespace nekotune {

IpcServer::IpcServer(PlayerEngine &player, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_serverName(defaultServerName())
{
    connect(&m_server, &QLocalServer::newConnection, this, &IpcServer::acceptConnection);
    connect(&m_player, &PlayerEngine::eventReady, this, &IpcServer::broadcastEvent);
}

bool IpcServer::listen()
{
    QLocalServer::removeServer(m_serverName);
    return m_server.listen(m_serverName);
}

QString IpcServer::serverName() const
{
    return m_serverName;
}

QString IpcServer::errorString() const
{
    return m_server.errorString();
}

void IpcServer::acceptConnection()
{
    while (auto *client = m_server.nextPendingConnection()) {
        m_buffers.insert(client, {});
        connect(client, &QLocalSocket::readyRead, this, &IpcServer::readClient);
        connect(client, &QLocalSocket::disconnected, this, [this, client]() {
            removeClient(client);
        });

        send(client, {
            {QStringLiteral("event"), QStringLiteral("server.connected")},
            {QStringLiteral("data"), m_player.status()},
        });
    }
}

void IpcServer::readClient()
{
    auto *client = qobject_cast<QLocalSocket *>(sender());
    if (!client) {
        return;
    }

    auto &buffer = m_buffers[client];
    buffer.append(client->readAll());

    qsizetype newline = -1;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const auto line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            send(client, {
                {QStringLiteral("status"), QStringLiteral("error")},
                {QStringLiteral("message"), QStringLiteral("Invalid JSON request")},
            });
            continue;
        }

        send(client, dispatch(document.object()));
    }
}

void IpcServer::removeClient(QObject *client)
{
    auto *socket = qobject_cast<QLocalSocket *>(client);
    if (!socket) {
        return;
    }

    m_buffers.remove(socket);
    socket->deleteLater();
}

void IpcServer::broadcastEvent(const QJsonObject &event)
{
    for (auto *client : m_buffers.keys()) {
        send(client, event);
    }
}

QString IpcServer::defaultServerName()
{
    const auto env = QProcessEnvironment::systemEnvironment();
    const auto explicitSocket = env.value(QStringLiteral("NEKOTUNE_SOCKET"));
    if (!explicitSocket.isEmpty()) {
        return explicitSocket;
    }

    return QStringLiteral("nekotune");
}

QJsonObject IpcServer::dispatch(const QJsonObject &request)
{
    const auto id = request.value(QStringLiteral("id"));
    const auto method = request.value(QStringLiteral("method")).toString();
    const auto params = request.value(QStringLiteral("params")).toObject();

    QJsonObject result;
    if (method == QStringLiteral("player.play")) {
        result = m_player.play(params);
    } else if (method == QStringLiteral("player.toggle_play_pause")) {
        result = m_player.togglePlayPause();
    } else if (method == QStringLiteral("player.pause")) {
        result = m_player.pause();
    } else if (method == QStringLiteral("player.stop")) {
        result = m_player.stop();
    } else if (method == QStringLiteral("player.next")) {
        result = m_player.next();
    } else if (method == QStringLiteral("player.previous")) {
        result = m_player.previous();
    } else if (method == QStringLiteral("player.seek")) {
        result = m_player.seek(static_cast<qint64>(params.value(QStringLiteral("position")).toDouble()));
    } else if (method == QStringLiteral("player.set_volume")) {
        result = m_player.setVolume(params.value(QStringLiteral("volume")).toDouble());
    } else if (method == QStringLiteral("player.status")) {
        result = {
            {QStringLiteral("status"), QStringLiteral("ok")},
            {QStringLiteral("data"), m_player.status()},
        };
    } else if (method == QStringLiteral("queue.add")) {
        result = m_player.addToQueue(params.value(QStringLiteral("path")).toString());
    } else if (method == QStringLiteral("queue.play")) {
        result = m_player.playQueueItem(params.value(QStringLiteral("id")).toInt());
    } else if (method == QStringLiteral("queue.remove")) {
        result = m_player.removeFromQueue(params.value(QStringLiteral("id")).toInt());
    } else if (method == QStringLiteral("queue.clear")) {
        result = m_player.clearQueue();
    } else if (method == QStringLiteral("queue.status")) {
        result = {
            {QStringLiteral("status"), QStringLiteral("ok")},
            {QStringLiteral("data"), m_player.queueStatus()},
        };
    } else if (method == QStringLiteral("song.metadata")) {
        result = m_player.songMetadata(params);
    } else if (method == QStringLiteral("song.update_metadata")) {
        result = m_player.updateSongMetadata(params);
    } else {
        result = {
            {QStringLiteral("status"), QStringLiteral("error")},
            {QStringLiteral("message"), QStringLiteral("Unknown method: %1").arg(method)},
        };
    }

    if (!id.isUndefined()) {
        result.insert(QStringLiteral("id"), id);
    }
    return result;
}

void IpcServer::send(QLocalSocket *client, const QJsonObject &payload)
{
    if (!client || client->state() != QLocalSocket::ConnectedState) {
        return;
    }

    client->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    client->write("\n");
    client->flush();
}

} // namespace nekotune
