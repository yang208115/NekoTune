#include "ipc_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcessEnvironment>
#include <QUrl>

IpcClient::IpcClient(QObject *parent)
    : QObject(parent)
{
    m_status.insert(QStringLiteral("state"), QStringLiteral("stopped"));
    m_status.insert(QStringLiteral("position"), 0);
    m_status.insert(QStringLiteral("duration"), 0);
    m_status.insert(QStringLiteral("volume"), 0.8);
    m_status.insert(QStringLiteral("queue"), QVariantList {});

    m_reconnectTimer.setInterval(1000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &IpcClient::connectBackend);
    connect(&m_socket, &QLocalSocket::connected, this, [this]() {
        m_reconnectTimer.stop();
        setError({});
        emit connectedChanged();
        refreshStatus();
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this]() {
        emit connectedChanged();
        if (!m_reconnectTimer.isActive()) {
            m_reconnectTimer.start();
        }
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &IpcClient::readMessages);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, &IpcClient::handleSocketError);

    connectBackend();
}

bool IpcClient::connected() const
{
    return m_socket.state() == QLocalSocket::ConnectedState;
}

QVariantMap IpcClient::status() const
{
    return m_status;
}

QString IpcClient::error() const
{
    return m_error;
}

void IpcClient::connectBackend()
{
    if (connected() || m_socket.state() == QLocalSocket::ConnectingState) {
        return;
    }

    m_socket.abort();
    m_socket.connectToServer(defaultServerName());
}

void IpcClient::playPath(const QString &path)
{
    sendRequest(QStringLiteral("player.play"), {{QStringLiteral("path"), normalizePath(path)}});
}

void IpcClient::addPath(const QString &path)
{
    sendRequest(QStringLiteral("queue.add"), {{QStringLiteral("path"), normalizePath(path)}});
}

void IpcClient::playQueueItem(int queueId)
{
    sendRequest(QStringLiteral("queue.play"), {{QStringLiteral("id"), queueId}});
}

void IpcClient::removeQueueItem(int queueId)
{
    sendRequest(QStringLiteral("queue.remove"), {{QStringLiteral("id"), queueId}});
}

void IpcClient::play()
{
    sendRequest(QStringLiteral("player.play"));
}

void IpcClient::togglePlayPause()
{
    sendRequest(QStringLiteral("player.toggle_play_pause"));
}

void IpcClient::pause()
{
    sendRequest(QStringLiteral("player.pause"));
}

void IpcClient::stop()
{
    sendRequest(QStringLiteral("player.stop"));
}

void IpcClient::next()
{
    sendRequest(QStringLiteral("player.next"));
}

void IpcClient::previous()
{
    sendRequest(QStringLiteral("player.previous"));
}

void IpcClient::clearQueue()
{
    sendRequest(QStringLiteral("queue.clear"));
}

void IpcClient::seek(double positionMs)
{
    sendRequest(QStringLiteral("player.seek"), {{QStringLiteral("position"), positionMs}});
}

void IpcClient::setVolume(double volume)
{
    sendRequest(QStringLiteral("player.set_volume"), {{QStringLiteral("volume"), volume}});
}

void IpcClient::updateSongMetadata(int songId,
                                   const QString &customTitle,
                                   const QString &artist,
                                   const QString &lyrics)
{
    sendRequest(QStringLiteral("song.update_metadata"), {
        {QStringLiteral("song_id"), songId},
        {QStringLiteral("custom_title"), customTitle},
        {QStringLiteral("artist"), artist},
        {QStringLiteral("lyrics"), lyrics},
    });
}

void IpcClient::refreshStatus()
{
    sendRequest(QStringLiteral("player.status"));
}

void IpcClient::readMessages()
{
    m_buffer.append(m_socket.readAll());

    qsizetype newline = -1;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const auto line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            setError(QStringLiteral("Backend returned invalid JSON"));
            continue;
        }
        handlePayload(document.object());
    }
}

void IpcClient::handleSocketError()
{
    if (connected()) {
        return;
    }

    setError(QStringLiteral("Waiting for NekoTune backend"));
    if (!m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
}

QString IpcClient::defaultServerName()
{
    const auto env = QProcessEnvironment::systemEnvironment();
    const auto explicitSocket = env.value(QStringLiteral("NEKOTUNE_SOCKET"));
    if (!explicitSocket.isEmpty()) {
        return explicitSocket;
    }

    return QStringLiteral("nekotune");
}

QString IpcClient::normalizePath(const QString &path)
{
    const QUrl url(path);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }

    return path;
}

void IpcClient::sendRequest(const QString &method, const QJsonObject &params)
{
    if (!connected()) {
        connectBackend();
        setError(QStringLiteral("Backend is not connected"));
        return;
    }

    const QJsonObject payload {
        {QStringLiteral("id"), m_nextId++},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    };
    m_socket.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_socket.write("\n");
    m_socket.flush();
}

void IpcClient::handlePayload(const QJsonObject &payload)
{
    const auto eventName = payload.value(QStringLiteral("event")).toString();
    if (!eventName.isEmpty()) {
        if (eventName == QStringLiteral("server.connected")) {
            mergeStatus(payload.value(QStringLiteral("data")).toObject());
        } else if (eventName == QStringLiteral("player.state_changed")) {
            m_status.insert(QStringLiteral("state"), payload.value(QStringLiteral("state")).toString());
            emit statusChanged();
        } else if (eventName == QStringLiteral("player.position_changed")) {
            m_status.insert(QStringLiteral("position"), payload.value(QStringLiteral("position")).toDouble());
            m_status.insert(QStringLiteral("duration"), payload.value(QStringLiteral("duration")).toDouble());
            emit statusChanged();
        } else if (eventName == QStringLiteral("player.duration_changed")) {
            m_status.insert(QStringLiteral("duration"), payload.value(QStringLiteral("duration")).toDouble());
            emit statusChanged();
        } else if (eventName == QStringLiteral("player.volume_changed")) {
            m_status.insert(QStringLiteral("volume"), payload.value(QStringLiteral("volume")).toDouble());
            emit statusChanged();
        } else if (eventName == QStringLiteral("player.track_changed")) {
            m_status.insert(QStringLiteral("song"), payload.value(QStringLiteral("song")).toObject().toVariantMap());
            emit statusChanged();
        } else if (eventName == QStringLiteral("queue.changed")) {
            m_status.insert(QStringLiteral("queue"), payload.value(QStringLiteral("queue")).toArray().toVariantList());
            emit statusChanged();
        } else if (eventName == QStringLiteral("player.error")) {
            setError(payload.value(QStringLiteral("message")).toString());
        }
        return;
    }

    if (payload.value(QStringLiteral("status")).toString() == QStringLiteral("error")) {
        setError(payload.value(QStringLiteral("message")).toString());
        return;
    }

    const auto data = payload.value(QStringLiteral("data")).toObject();
    if (!data.isEmpty()) {
        mergeStatus(data);
    }
}

void IpcClient::mergeStatus(const QJsonObject &data)
{
    const auto map = data.toVariantMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        m_status.insert(it.key(), it.value());
    }

    emit statusChanged();
}

void IpcClient::setError(const QString &message)
{
    if (m_error == message) {
        return;
    }

    m_error = message;
    emit errorChanged();
}
