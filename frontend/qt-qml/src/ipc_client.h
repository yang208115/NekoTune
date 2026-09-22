#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QHash>

class IpcClient final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit IpcClient(QObject *parent = nullptr);

    bool connected() const;
    QVariantMap status() const;
    QString error() const;

    Q_INVOKABLE void connectBackend();
    Q_INVOKABLE void playPath(const QString &path);
    Q_INVOKABLE void addPath(const QString &path);
    Q_INVOKABLE void playQueueItem(int queueId);
    Q_INVOKABLE void removeQueueItem(int queueId);
    Q_INVOKABLE void play();
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void clearQueue();
    Q_INVOKABLE void seek(double positionMs);
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void updateSongMetadata(int songId,
                                        const QString &customTitle,
                                        const QString &artist,
                                        const QString &lyrics);
    Q_INVOKABLE void refreshStatus();

signals:
    void connectedChanged();
    void statusChanged();
    void errorChanged();

private slots:
    void readMessages();
    void handleSocketError();

private:
    static QString defaultServerName();
    static QString normalizePath(const QString &path);

    void sendRequest(const QString &method, const QJsonObject &params = {});
    void handlePayload(const QJsonObject &payload);
    void mergeStatus(const QJsonObject &data);
    void setError(const QString &message);

    QLocalSocket m_socket;
    QTimer m_reconnectTimer;
    QByteArray m_buffer;
    QVariantMap m_status;
    QString m_error;
    int m_nextId = 1;
    QHash<int, QString> m_pendingRequests;
};
