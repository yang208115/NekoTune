#include "ipc_client.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>
IpcClient::IpcClient(QObject *parent) : QObject(parent) {
    m_reconnectTimer.setInterval(1000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &IpcClient::connectBackend);
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_reconnectTimer.stop();
        setError({});
        emit connectedChanged();
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        m_buffer.clear();
        failPending();
        setError("Backend disconnected");
        emit connectedChanged();
        m_reconnectTimer.start();
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &IpcClient::readMessages);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this] {
        if (!connected()) {
            setError("Waiting for NekoTune backend");
            m_reconnectTimer.start();
        }
    });
    QTimer::singleShot(0, this, &IpcClient::connectBackend);
}
IpcClient::~IpcClient() {
    m_reconnectTimer.stop();
    disconnect(&m_socket, nullptr, this, nullptr);
    m_socket.abort();
    m_pending.clear();
}
void IpcClient::connectBackend() {
    if (connected() || m_socket.state() == QLocalSocket::ConnectingState)
        return;
    m_socket.abort();
    m_socket.connectToServer(qEnvironmentVariable("NEKOTUNE_SOCKET", QStringLiteral("nekotune")));
}
QString IpcClient::normalizePath(const QString &path) {
    QUrl url(path);
    return url.isLocalFile() ? url.toLocalFile() : path;
}
void IpcClient::request(const QString &method, const QJsonObject &params, Completion completion) {
    if (!connected()) {
        connectBackend();
        const QString message = "Backend is not connected";
        setError(message);
        if (completion)
            completion({}, message);
        emit requestFailed(method, message);
        return;
    }
    auto id = m_nextId++;
    m_pending.insert(id, {method, std::move(completion)});
    m_socket.write(QJsonDocument(QJsonObject{{"id", id}, {"method", method}, {"params", params}})
                       .toJson(QJsonDocument::Compact) +
                   "\n");
}
void IpcClient::failPending() {
    auto pending = std::move(m_pending);
    m_pending.clear();
    for (const auto &request : pending) {
        if (request.completion)
            request.completion({}, "Backend disconnected");
        emit requestFailed(request.method, "Backend disconnected");
    }
}
void IpcClient::readMessages() {
    m_buffer.append(m_socket.readAll());
    if (m_buffer.size() > 64 * 1024 * 1024) {
        setError("Backend response too large");
        m_socket.abort();
        return;
    }
    while (m_buffer.contains('\n')) {
        auto end = m_buffer.indexOf('\n');
        auto line = m_buffer.left(end);
        m_buffer.remove(0, end + 1);
        QJsonParseError parse;
        auto json = QJsonDocument::fromJson(line, &parse);
        if (parse.error != QJsonParseError::NoError || !json.isObject()) {
            setError("Backend returned invalid JSON");
            continue;
        }
        auto payload = json.object();
        if (payload.contains("event")) {
            if (payload.value("event") == "player.error")
                setError(payload.value("message").toString());
            emit eventReceived(payload);
            continue;
        }
        auto id = payload.value("id").toInteger(-1);
        if (!m_pending.contains(id))
            continue;
        auto pending = m_pending.take(id);
        auto message = payload.value("status") == "error" ? payload.value("message").toString() : QString();
        auto data = payload.value("data").toObject();
        if (!message.isEmpty()) {
            setError(message);
            if (pending.completion)
                pending.completion({}, message);
            emit requestFailed(pending.method, message);
            continue;
        }
        if (pending.completion)
            pending.completion(data, {});
        emit responseReceived(pending.method, data);
        emit requestSucceeded(pending.method);
    }
}
void IpcClient::setError(const QString &value) {
    if (m_error == value)
        return;
    m_error = value;
    emit errorChanged();
}
