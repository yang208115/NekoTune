#pragma once
#include "ipc_client.h"
class FeatureController : public QObject {
    Q_OBJECT
  public:
    explicit FeatureController(IpcClient &client, QObject *parent = nullptr)
        : QObject(parent), m_client(client) {}
  signals:
    void requestSucceeded(const QString &method);
    void requestFailed(const QString &method, const QString &message);

  protected:
    void send(const QString &method, const QJsonObject &params = {}, IpcClient::Completion completion = {});
    IpcClient &m_client;
};
