#pragma once
#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <functional>
class IpcClient final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  public:
    using Completion = std::function<void(const QJsonObject &, const QString &)>;
    explicit IpcClient(QObject *parent = nullptr);
    ~IpcClient() override;
    bool connected() const { return m_socket.state() == QLocalSocket::ConnectedState; }
    QString error() const { return m_error; }
    void request(const QString &method, const QJsonObject &params = {}, Completion completion = {},
                 bool reportError = true);
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
