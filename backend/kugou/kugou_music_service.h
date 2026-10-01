#pragma once
#include "kugou/kugou_download_job.h"
namespace nekotune {
class KugouMusicService final : public IKugouBackend {
    Q_OBJECT
  public:
    explicit KugouMusicService(QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr,
                               const QUrl &baseUrl = {}, const QString &sessionPath = {},
                               const QString &musicDirectory = {}, const QString &keyPath = {});
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
};
} // namespace nekotune
