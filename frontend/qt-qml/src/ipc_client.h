#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>
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
    Q_INVOKABLE void managePlaylist(const QString &action, const QVariantMap &params);
    Q_INVOKABLE void manageTag(const QString &action, const QVariantMap &params);
    Q_INVOKABLE void refreshLibrary();
    Q_INVOKABLE void importLibraryPath(const QString &path);
    Q_INVOKABLE void kugouSendCode(const QString &mobile);
    Q_INVOKABLE void kugouSaveKey(const QString &key);
    Q_INVOKABLE void kugouClearKey();
    Q_INVOKABLE void kugouLogin(const QString &mobile, const QString &code);
    Q_INVOKABLE void kugouSearch(const QString &keywords, int page);
    Q_INVOKABLE void kugouDownload(const QString &hash);
    Q_INVOKABLE void kugouCancel();
    Q_INVOKABLE void playLibrary(const QVariantList &tagIds, int songId = 0);
    Q_INVOKABLE void deleteLibrarySongs(const QVariantList &songIds);
    Q_INVOKABLE void clearQueue();
    Q_INVOKABLE void seek(double positionMs);
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void updateSongMetadata(int songId, const QString &customTitle, const QString &artist,
                                        const QString &lyrics, const QVariantList &tags);
    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE void refreshLyrics(const QString &trackId);
    Q_INVOKABLE void searchLyrics(const QString &trackId, const QString &title, const QString &artist,
                                  const QString &album, const QString &source = QStringLiteral("lrclib"));
    Q_INVOKABLE void selectLyrics(const QString &trackId, const QString &revision, int index);
    Q_INVOKABLE void setLyricsOffline(bool offline);

signals:
    void connectedChanged();
    void statusChanged();
    void errorChanged();
    void songMetadataSaved(int songId);
    void libraryPlaybackSkipped(int count);
    void requestFailed(const QString &method, const QString &message);
    void requestSucceeded(const QString &method);
    void kugouEvent(const QVariantMap &event);

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
