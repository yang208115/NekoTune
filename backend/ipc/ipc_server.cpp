#include "ipc/ipc_server.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcessEnvironment>

namespace nekotune {

IpcServer::IpcServer(PlayerEngine &player, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_router(player)
    , m_serverName(defaultServerName())
{
    connect(&m_server, &QLocalServer::newConnection, this, &IpcServer::acceptConnection);
    connect(&m_player, &PlayerEngine::eventReady, this, &IpcServer::broadcastEvent);
}

bool IpcServer::listen()
{
    if (m_server.listen(m_serverName)) {
        return true;
    }

    if (m_server.serverError() != QAbstractSocket::AddressInUseError) {
        return false;
    }

    QLocalSocket probe;
    probe.connectToServer(m_serverName);
    if (!probe.waitForConnected(150)) {
        QLocalServer::removeServer(m_serverName);
        return m_server.listen(m_serverName);
    }
    m_error = QStringLiteral("NekoTune backend is already running");
    return false;
}

QString IpcServer::serverName() const
{
    return m_serverName;
}

QString IpcServer::errorString() const
{
    return m_error.isEmpty() ? m_server.errorString() : m_error;
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

        send(client, m_router.dispatch(document.object()));
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
