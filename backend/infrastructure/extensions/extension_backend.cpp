#include "infrastructure/extensions/extension_backend.h"
#include "app_paths.h"
#include "infrastructure/kugou/kugou_account_session.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QUuid>
#include <signal.h>

namespace nekotune {
ExtensionBackend::ExtensionBackend(QObject *parent) : IExtensionBackend(parent) {
    m_startTimer.setSingleShot(true);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        fail("Extension supervisor startup timed out");
        m_process.kill();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this] {
        if (!m_stopping)
            fail(m_process.errorString());
    });
    connect(&m_process, &QProcess::finished, this, [this] {
        killWorkers();
        if (!m_stopping)
            fail("Extension supervisor stopped");
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this,
            [this] { m_process.readAllStandardOutput(); });
    connect(&m_process, &QProcess::readyReadStandardError, this,
            [this] { m_process.readAllStandardError(); });
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection()) {
            if (m_socket) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            m_socket = socket;
            connect(socket, &QLocalSocket::readyRead, this, &ExtensionBackend::read);
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                if (m_socket == socket) {
                    m_socket = nullptr;
                    m_authenticated = false;
                    if (!m_stopping)
                        fail("Extension supervisor disconnected");
                }
                socket->deleteLater();
            });
        }
    });
}
ExtensionBackend::~ExtensionBackend() { shutdown(); }
void ExtensionBackend::start(HostCall hostCall) {
    m_hostCall = std::move(hostCall);
    m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen("nekotune-extensions-" + m_token)) {
        fail(m_server.errorString());
        return;
    }
    const QDir appDirectory(QCoreApplication::applicationDirPath());
    QString node = appDirectory.filePath("../libexec/nekotune/node");
    QString script = appDirectory.filePath("../libexec/nekotune/supervisor.cjs");
    if (!QFileInfo::exists(node)) {
        node = QStringLiteral(NEKOTUNE_NODE_BINARY);
        script = QStringLiteral(NEKOTUNE_EXTENSION_SUPERVISOR);
    }
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("NEKOTUNE_EXTENSION_SOCKET", m_server.fullServerName());
    environment.insert("NEKOTUNE_EXTENSION_TOKEN", m_token);
    environment.insert("NEKOTUNE_EXTENSIONS_ROOT", AppPaths::extensionsDirectory());
    environment.insert("NEKOTUNE_BUNDLED_EXTENSIONS", QFileInfo(script).absolutePath() + "/builtin");
    m_process.setProcessEnvironment(environment);
    m_process.start(node, {script});
    m_startTimer.start(10'000);
}
void ExtensionBackend::send(const QVariantMap &message) {
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState)
        return;
    m_socket->write(QJsonDocument(QJsonObject::fromVariantMap(message)).toJson(QJsonDocument::Compact) +
                    '\n');
}
void ExtensionBackend::request(const QString &method, const QVariantMap &params, Completion done) {
    if (!m_authenticated || !m_snapshot.value("catalogueReady").toBool() || m_stopping) {
        done(failure("Extension runtime is not available"));
        return;
    }
    const auto id = m_nextId++;
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    m_pending.insert(id, {std::move(done), timer});
    connect(timer, &QTimer::timeout, this, [this, id] {
        auto pending = m_pending.take(id);
        if (!pending.timer)
            return;
        pending.timer->deleteLater();
        pending.done(failure("Extension request timed out"));
    });
    timer->start(method == "extensions.download_audio"       ? 610'000
                 : method == "extensions.download_completed" ? 125'000
                                                             : 30'000);
    send({{"id", id}, {"method", method}, {"params", params}});
}
void ExtensionBackend::publish(const QVariantMap &event) {
    if (m_authenticated && !m_stopping)
        send({{"method", "host.event"}, {"params", event}});
}
void ExtensionBackend::read() {
    m_buffer += m_socket->readAll();
    if (m_buffer.size() > 8 * 1024 * 1024) {
        fail("Extension frame too large");
        m_socket->abort();
        return;
    }
    qsizetype newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const auto line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        const auto document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            fail("Invalid extension frame");
            return;
        }
        receive(document.object().toVariantMap());
    }
}
void ExtensionBackend::receive(const QVariantMap &message) {
    const auto method = message.value("method").toString();
    const auto params = message.value("params").toMap();
    const auto id = message.value("id").toLongLong();
    if (!m_authenticated) {
        if (method != "hello" || params.value("token").toString() != m_token) {
            if (m_socket)
                m_socket->abort();
            return;
        }
        m_authenticated = true;
        m_startTimer.stop();
        send({{"id", id}, {"status", "ok"}, {"data", QVariantMap{}}});
        return;
    }
    if (message.contains("status")) {
        auto pending = m_pending.take(id);
        if (!pending.timer)
            return;
        pending.timer->stop();
        pending.timer->deleteLater();
        if (message.value("status") == "ok")
            pending.done(message.value("data").toMap());
        else
            pending.done(failure(message.value("message").toString()));
        return;
    }
    const auto event = message.value("event").toString();
    if (!event.isEmpty()) {
        if (event == "extensions.process") {
            if (message.value("active").toBool())
                m_workerPids.insert(message.value("pid").toLongLong());
            else
                m_workerPids.remove(message.value("pid").toLongLong());
            return;
        }
        if (event == "extensions.changed") {
            m_snapshot = message;
            m_snapshot.remove("event");
            emit changed();
        }
        emit eventReady(message);
        return;
    }
    QPointer<ExtensionBackend> guard(this);
    auto done = [guard, id](Result<QVariantMap> result) {
        if (!guard)
            return;
        guard->send(result
                        ? QVariantMap{{"id", id}, {"status", "ok"}, {"data", result.value()}}
                        : QVariantMap{{"id", id}, {"status", "error"}, {"message", result.error().message}});
    };
    if (method == "host.call") {
        if (m_hostCall)
            m_hostCall(params.value("method").toString(), params.value("params").toMap(), done);
        else
            done(failure("Host unavailable"));
    } else if (method == "secret") {
        // These two keys retain their pre-plugin secure-store identity. Existing verified migration
        // handles old plaintext/libsecret data; secrets never pass through public business IPC.
        const auto extensionId = params.value("extensionId").toString();
        const auto secretKey = params.value("key").toString();
        const auto action = params.value("operation").toString();
        if (extensionId == "nekotune.kugou" &&
            (secretKey == "account-key" || secretKey == "account-session")) {
            KugouAccountSession legacy(AppPaths::configFile("kugou-account-key"),
                                       AppPaths::configFile("kugou-session.json"));
            const bool accountKey = secretKey == "account-key";
            QString error;
            if (action == "get") {
                error = accountKey ? legacy.keyError : legacy.sessionError;
                QVariant value;
                if (accountKey && legacy.keySaved)
                    value = legacy.key;
                else if (!accountKey && !legacy.cookies.isEmpty()) {
                    QJsonObject cookies;
                    for (auto it = legacy.cookies.cbegin(); it != legacy.cookies.cend(); ++it)
                        cookies.insert(it.key(), it.value());
                    value = QString::fromUtf8(QJsonDocument(QJsonObject{{"version", 1}, {"cookies", cookies}})
                                                  .toJson(QJsonDocument::Compact));
                }
                done(error.isEmpty() ? Result<QVariantMap>(QVariantMap{{"value", value}})
                                     : Result<QVariantMap>(failure(error)));
                return;
            }
            if (action == "delete")
                error = accountKey ? legacy.clearAccountKey() : legacy.clearSession();
            else if (action == "set" && accountKey)
                error = legacy.saveAccountKey(params.value("value").toString());
            else if (action == "set") {
                const auto document = QJsonDocument::fromJson(params.value("value").toString().toUtf8());
                const auto object = document.object();
                if (!document.isObject() || object.value("version").toInt() != 1 ||
                    !object.value("cookies").isObject())
                    error = "Invalid session data";
                else {
                    legacy.cookies.clear();
                    const auto cookies = object.value("cookies").toObject();
                    for (auto it = cookies.begin(); it != cookies.end(); ++it) {
                        if (!it.value().isString()) {
                            error = "Invalid session data";
                            break;
                        }
                        legacy.cookies.insert(it.key(), it.value().toString());
                    }
                    if (error.isEmpty() && !legacy.saveSession())
                        error = legacy.sessionError;
                }
            } else
                error = "Unknown credential operation";
            done(error.isEmpty() ? Result<QVariantMap>(QVariantMap{}) : Result<QVariantMap>(failure(error)));
            return;
        }
        const auto suffix = QString::fromLatin1(
            QCryptographicHash::hash(params.value("key").toString().toUtf8(), QCryptographicHash::Sha256)
                .toHex());
        const auto key =
            AppPaths::extensionsDirectory() + "/" + params.value("extensionId").toString() + "/" + suffix;
        const auto store = systemCredentialStore();
        const auto operation = params.value("operation").toString();
        if (operation == "get") {
            auto value = store->read(key);
            if (!value)
                done(value.error());
            else
                done(QVariantMap{
                    {"value", value.value() ? QVariant(QString::fromUtf8(*value.value())) : QVariant()}});
        } else if (operation == "set" || operation == "delete") {
            auto value = operation == "set" ? store->write(key, params.value("value").toString().toUtf8())
                                            : store->remove(key);
            if (!value)
                done(value.error());
            else
                done(QVariantMap{});
        } else
            done(failure("Unknown credential operation"));
    } else
        done(failure("Unknown supervisor request"));
}
void ExtensionBackend::fail(const QString &message) {
    m_startTimer.stop();
    const auto pending = std::exchange(m_pending, {});
    for (const auto &item : pending) {
        item.timer->stop();
        item.timer->deleteLater();
        item.done(failure(message));
    }
    auto extensions = m_snapshot.value("extensions").toList();
    for (auto &value : extensions) {
        auto entry = value.toMap();
        entry.insert("state", "failed");
        entry.insert("error", message);
        entry.insert("contributes", QVariantMap{});
        entry.insert("registrations", QVariantMap{});
        value = entry;
    }
    m_snapshot.insert("extensions", extensions);
    emit changed();
    emit eventReady({{"event", "extensions.changed"},
                     {"extensions", extensions},
                     {"selections", m_snapshot.value("selections")}});
    emit eventReady({{"event", "extensions.error"}, {"message", message}});
}
void ExtensionBackend::killWorkers() {
    for (const auto pid : std::exchange(m_workerPids, {}))
        if (pid > 1)
            ::kill(-static_cast<pid_t>(pid), SIGKILL);
}
void ExtensionBackend::shutdown() {
    if (m_stopping)
        return;
    m_stopping = true;
    m_startTimer.stop();
    if (m_socket) {
        send({{"method", "shutdown"}});
        m_socket->flush();
    }
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(3500)) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }
    killWorkers();
    m_server.close();
    fail("Extension runtime stopped");
}
} // namespace nekotune
