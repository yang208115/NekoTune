#pragma once
#include "infrastructure/kugou/kugou_api_client.h"
#include "infrastructure/kugou/kugou_song.h"
#include <QSaveFile>
#include <memory>
namespace nekotune {
class KugouDownloadJob final : public QObject {
    Q_OBJECT
  public:
    KugouDownloadJob(QNetworkAccessManager &manager, KugouApiClient &api, QString directory)
        : m_manager(&manager), m_api(api), m_musicDirectory(std::move(directory)) {}
    void prepare(const KugouSong &song, const QString &base = {}) {
        m_destinationBase = base;
        m_selected = song;
        m_downloadActive = true;
        m_cancelled = false;
        m_audioPath.clear();
        m_audioError.clear();
    }
    bool reuseExisting();
    void beginAudio(const QUrl &url, int redirects = 0);
    QString cancelDownload();
    void shutdown();
    static bool trustedAudioUrl(const QUrl &url);
    static QUrl trustedCoverUrl(const QString &image);
  signals:
    void eventReady(const nekotune::KugouEvent &event);
    void audioReady(const QString &path, const QString &lyricStatus, const QString &coverStatus,
                    const QString &title, const QString &artist);

  private:
    void requestJson(const QString &route, const QJsonObject &body, KugouApiClient::JsonCallback callback) {
        m_api.requestJson(route, body, std::move(callback));
    }
    void finishOperation(KugouEventType event, const QString &message = {});
    void discardAudio();
    void fetchLyrics();
    void fetchKrc(const QJsonObject &candidate, std::function<void(const QString &)> callback);
    void fetchLrc(const QJsonObject &candidate, const QString &krcStatus);
    void finishAudio(const QString &lyricStatus);
    void fetchCover(const QString &lyricStatus, const QUrl &url, int redirects = 0);
    void completeAudio(const QString &lyricStatus, const QString &coverStatus);
    static QString safeName(const QString &value);
    static QString normalizedName(const QString &value);
    static bool trustedCoverRedirect(const QUrl &url);
    QNetworkAccessManager *m_manager;
    KugouApiClient &m_api;
    QString m_musicDirectory;
    QString m_destinationBase;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QSaveFile> m_audioFile;
    QString m_audioPath, m_audioError;
    KugouSong m_selected;
    qint64 m_audioBytes = 0;
    bool m_downloadActive = false, m_cancelled = false;
};
} // namespace nekotune
