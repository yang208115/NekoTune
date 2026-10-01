#pragma once

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>

#include <functional>
#include <memory>

namespace nekotune {

class KugouMusicService final : public QObject {
    Q_OBJECT
public:
    explicit KugouMusicService(QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr,
                               const QUrl &baseUrl = {}, const QString &sessionPath = {},
                               const QString &musicDirectory = {}, const QString &keyPath = {});
    QJsonObject status() const;
    QString saveAccountKey(const QString &key);
    QString clearAccountKey();
    QString startCodeRequest(const QString &mobile);
    QString startLogin(const QString &mobile, const QString &code);
    QString startSearch(const QString &keywords, int page);
    QString startDownload(const QString &hash);
    QString cancelDownload();

signals:
    void eventReady(const QJsonObject &event);
    void audioReady(const QString &path, const QString &lyricStatus, const QString &coverStatus,
                    const QString &title, const QString &artist);

private:
    using JsonCallback = std::function<void(const QJsonObject &, const QString &)>;
    struct Song {
        QString hash;
        QString title;
        QString artist;
        QString album;
        QString audioId;
        QUrl coverUrl;
        qint64 durationMs = 0;
    };

    void requestJson(const QString &route, const QJsonObject &body, JsonCallback callback);
    void accountRequest(const QString &route, const QJsonObject &body, JsonCallback callback);
    bool saveSession();
    void loadSession();
    void loadKey();
    void finishOperation(const QString &event, const QString &message = {});
    void fetchLyrics();
    void fetchKrc(const QJsonObject &candidate, std::function<void(const QString &)> callback);
    void fetchLrc(const QJsonObject &candidate, const QString &krcStatus);
    void finishAudio(const QString &lyricStatus);
    void fetchCover(const QString &lyricStatus, const QUrl &url, int redirects = 0);
    void completeAudio(const QString &lyricStatus, const QString &coverStatus);
    void beginAudio(const QUrl &url, int redirects = 0);
    void discardAudio();
    static QString safeName(const QString &value);
    static QString normalizedName(const QString &value);
    static bool trustedAudioUrl(const QUrl &url);
    static QUrl trustedCoverUrl(const QString &image);
    static bool trustedCoverRedirect(const QUrl &url);
    static bool businessOk(const QJsonObject &body, const QString &expected);

    QNetworkAccessManager *m_manager;
    QUrl m_baseUrl;
    QString m_key;
    QString m_configurationError;
    QString m_keyPath;
    bool m_keySaved = false;
    QString m_sessionPath;
    QString m_musicDirectory;
    QHash<QString, QString> m_cookies;
    QHash<QString, Song> m_songs;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QSaveFile> m_audioFile;
    QString m_audioPath;
    QString m_audioError;
    Song m_selected;
    qint64 m_audioBytes = 0;
    bool m_busy = false;
    bool m_downloadActive = false;
    bool m_cancelled = false;
};

} // namespace nekotune
