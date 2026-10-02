#pragma once
#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <functional>
/// Multiplexes response callbacks by request ID and forwards independent events to feature controllers.
class IpcClient final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  public:
    /// The data object is meaningful on success; a nonempty message represents failure.
    /// Callbacks run on the frontend thread and can complete synchronously when disconnected.
    /// Consumers must be prepared for connection failure as well as backend method errors.
    using Completion = std::function<void(const QJsonObject &, const QString &)>;
    explicit IpcClient(QObject *parent = nullptr);
    ~IpcClient() override;
    bool connected() const { return m_socket.state() == QLocalSocket::ConnectedState; }
    QString error() const { return m_error; }
    /// @param method Registered backend method name used for request/result correlation.
    /// @param params JSON values conforming to that method's public contract.
    /// @param completion Optional callback consumed once on response or connection loss.
    /// @param reportError Whether failure should also update the shared transport banner.
    /// Requests are not replayed after reconnect because a lost reply may follow a committed mutation.
    /// Lifetime-sensitive callers should guard their objects through FeatureController::send.
    void request(const QString &method, const QJsonObject &params = {}, Completion completion = {},
                 bool reportError = true);
    /// Convert file URLs returned by QML dialogs into native local paths.
    /// Already-native paths are retained verbatim rather than decoded as arbitrary URLs.
    /// Filesystem validation remains the receiving backend method's responsibility.
    static QString normalizePath(const QString &path);
    Q_INVOKABLE void connectBackend();
  signals:
    void connectedChanged();
    void errorChanged();
    void eventReceived(const QJsonObject &event);
    void responseReceived(const QString &method, const QJsonObject &data);
    void requestSucceeded(const QString &method);
    void requestFailed(const QString &method, const QString &message);

  private:
    struct Pending {
        QString method;
        Completion completion;
        bool reportError = true;
    };
    void readMessages();
    void setError(const QString &message);
    void failPending();
    QLocalSocket m_socket;
    QTimer m_reconnectTimer;
    QByteArray m_buffer;
    QString m_error;
    QHash<qint64, Pending> m_pending;
    qint64 m_nextId = 1;
};
