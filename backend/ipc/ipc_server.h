#pragma once
#include "ipc/ipc_router.h"
#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
namespace nekotune {
class IpcServer final : public QObject {
    Q_OBJECT
  public:
    explicit IpcServer(IpcRouter &router, std::function<QJsonObject()> snapshot, QObject *parent = nullptr);
    ~IpcServer() override;
    bool listen();
    QString serverName() const;
    QString errorString() const;
    void stopAccepting();
    void shutdown();
    void broadcastEvent(const QJsonObject &event);

  private:
    void acceptConnection();
    void readClient(QLocalSocket *client);
    void removeClient(QLocalSocket *client);
    void send(QLocalSocket *client, const QJsonObject &payload);
    static QString defaultServerName();
    IpcRouter &m_router;
    std::function<QJsonObject()> m_snapshot;
    QLocalServer m_server;
    QString m_serverName;
    QString m_error;
    QHash<QLocalSocket *, QByteArray> m_buffers;
};
} // namespace nekotune
