#pragma once
#include "domain/result.h"
#include "infrastructure/kugou/kugou_download_job.h"
#include <functional>
namespace nekotune {
/// Coordinates search/account state and a single download pipeline.
/// Search records retain trusted provider data used by later download.
/// Download accepts hashes from the current search snapshot only.
/// The destination callback reserves a managed basename before transfer.
/// Audio transfer, lyric acquisition and cover acquisition are separate stages.
/// Saved audio emits audioReady and is imported by application wiring.
/// No queue changes or playback decisions belong in this adapter.
class KugouMusicService final : public IKugouBackend {
    Q_OBJECT
  public:
    explicit KugouMusicService(
        QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr, const QUrl &baseUrl = {},
        const QString &sessionPath = {}, const QString &musicDirectory = {}, const QString &keyPath = {},
        std::function<Result<QString>(const QString &, const QString &)> destination = {});
    ~KugouMusicService() override;
    KugouStatus status() const override;
    void shutdown();
    QString saveAccountKey(const QString &key) override;
    QString clearAccountKey() override;
    QString startCodeRequest(const QString &mobile) override;
    QString startLogin(const QString &mobile, const QString &code) override;
    QString startSearch(const QString &keywords, int page) override;
    QString startDownload(const QString &hash) override;
    QString cancelDownload() override;

  private:
    void finishOperation(KugouEventType event, const QString &message = {});
    static bool businessOk(const QJsonObject &body, const QString &expected);
    QNetworkAccessManager *m_manager;
    QUrl m_baseUrl;
    QString m_configurationError, m_keyPath, m_sessionPath, m_musicDirectory;
    std::unique_ptr<KugouAccountSession> m_account;
    std::unique_ptr<KugouApiClient> m_api;
    std::unique_ptr<KugouDownloadJob> m_download;
    QHash<QString, KugouSong> m_songs;
    KugouSong m_selected;
    bool m_busy = false, m_downloadActive = false, m_cancelled = false;
    std::function<Result<QString>(const QString &, const QString &)> m_destination;
};
} // namespace nekotune
