#pragma once
#include "ipc_client.h"
/// Common request bridge for feature-specific frontend state owners.
/// It does not mirror the entire backend snapshot in one global object.
/// send() guards this controller while an IPC callback is pending.
/// Per-feature success/failure signals support local dialog feedback.
/// reportError controls the shared transport error banner independently.
class FeatureController : public QObject {
    Q_OBJECT
  public:
    explicit FeatureController(IpcClient &client, QObject *parent = nullptr)
        : QObject(parent), m_client(client) {}
  signals:
    void requestSucceeded(const QString &method);
    void requestFailed(const QString &method, const QString &message);

  protected:
    /// Wrap IPC completion with a guarded pointer to the feature controller.
    /// Destroying a page/controller while awaiting a reply must not invoke its captured callbacks.
    /// Local failure/success signals retain the method name for dialog-specific completion handling.
    /// The optional shared error banner is independent of those feature notifications.
    void send(const QString &method, const QJsonObject &params = {}, IpcClient::Completion completion = {},
              bool reportError = true);
    IpcClient &m_client;
};
