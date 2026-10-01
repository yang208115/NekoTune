#include "ipc/ipc_server.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QPointer>
#include <QProcessEnvironment>

namespace nekotune {

IpcServer::IpcServer(IpcRouter &router, std::function<QJsonObject()> snapshot, QObject *parent)
    : QObject(parent), m_router(router), m_snapshot(std::move(snapshot)), m_serverName(defaultServerName()) {
    connect(&m_server, &QLocalServer::newConnection, this, &IpcServer::acceptConnection);
}
IpcServer::~IpcServer() { shutdown(); }
void IpcServer::stopAccepting() { m_server.close(); }
void IpcServer::shutdown() {
    stopAccepting();
    const auto clients = m_buffers.keys();
    m_buffers.clear();
    for (auto *client : clients) {
        disconnect(client, nullptr, this, nullptr);
        client->abort();
        delete client;
    }
}

bool IpcServer::listen() {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
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

QString IpcServer::serverName() const { return m_serverName; }

QString IpcServer::errorString() const { return m_error.isEmpty() ? m_server.errorString() : m_error; }

void IpcServer::acceptConnection() {
    while (auto *client = m_server.nextPendingConnection()) {
        m_buffers.insert(client, {});
        connect(client, &QLocalSocket::readyRead, this, [this, client] { readClient(client); });
        connect(client, &QLocalSocket::disconnected, this, [this, client]() { removeClient(client); });

        send(client, {
                         {QStringLiteral("event"), QStringLiteral("server.connected")},
                         {QStringLiteral("data"), m_snapshot()},
                     });
    }
}

void IpcServer::readClient(QLocalSocket *client) {
    auto &buffer = m_buffers[client];
    buffer.append(client->readAll());
    if (buffer.size() > 32 * 1024 * 1024) {
        client->disconnectFromServer();
        return;
    }

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

        QPointer<QLocalSocket> guard(client);
        m_router.dispatch(document.object(), [this, guard](QJsonObject response) {
            if (guard)
                send(guard, response);
        });
    }
}

void IpcServer::removeClient(QLocalSocket *client) {
    auto *socket = client;
    if (!socket) {
        return;
    }

    m_buffers.remove(socket);
    socket->deleteLater();
}

void IpcServer::broadcastEvent(const QJsonObject &event) {
    for (auto *client : m_buffers.keys()) {
        send(client, event);
    }
}

QString IpcServer::defaultServerName() {
    const auto env = QProcessEnvironment::systemEnvironment();
    const auto explicitSocket = env.value(QStringLiteral("NEKOTUNE_SOCKET"));
    if (!explicitSocket.isEmpty()) {
        return explicitSocket;
    }

    return QStringLiteral("nekotune");
}

void IpcServer::send(QLocalSocket *client, const QJsonObject &payload) {
    if (!client || client->state() != QLocalSocket::ConnectedState) {
        return;
    }

    client->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    client->write("\n");
    client->flush();
}

} // namespace nekotune
