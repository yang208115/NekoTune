#pragma once
#include "domain/extensions/extension_backend.h"
#include "infrastructure/credentials/credential_store.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QSet>
#include <QTimer>

namespace nekotune {
class ExtensionBackend final : public IExtensionBackend {
    Q_OBJECT
  public:
    explicit ExtensionBackend(QObject *parent = nullptr);
    ~ExtensionBackend() override;
    void start(HostCall hostCall);
    void shutdown();
    void request(const QString &, const QVariantMap &, Completion) override;
    QVariantMap snapshot() const override {
        auto result = m_snapshot;
        result.insert("runtimeReady", m_authenticated && m_snapshot.value("catalogueReady").toBool());
        return result;
    }
    void publish(const QVariantMap &) override;

  private:
    struct Pending {
        Completion done;
        QTimer *timer;
    };
    void read();
    void send(const QVariantMap &message);
    void receive(const QVariantMap &message);
    void fail(const QString &message);
    void killWorkers();
    QLocalServer m_server;
    QProcess m_process;
    QLocalSocket *m_socket = nullptr;
    QTimer m_startTimer;
    QByteArray m_buffer;
    QString m_token;
    QHash<qint64, Pending> m_pending;
    QSet<qint64> m_workerPids;
    qint64 m_nextId = 1;
    QVariantMap m_snapshot{{"extensions", QVariantList{}}, {"selections", QVariantMap{}}};
    HostCall m_hostCall;
    bool m_authenticated = false;
    bool m_stopping = false;
};
} // namespace nekotune
