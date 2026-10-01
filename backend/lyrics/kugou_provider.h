#pragma once

#include "domain/lyrics_provider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QUrlQuery>
#include <functional>

namespace nekotune {

class KugouProvider final : public LyricsProvider {
    Q_OBJECT
  public:
    explicit KugouProvider(
        QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr,
        const QUrl &baseUrl = QUrl(QStringLiteral("https://kugou-lyrics-api.lyuy.workers.dev")));
    ~KugouProvider() override;
    void request(const LyricsQuery &query, quint64 token, bool search) override;
    void choose(const LyricsCandidate &candidate, quint64 token) override;
    LyricsSource descriptor() const override {
        return {QStringLiteral("kugou"), QStringLiteral("酷狗音乐"), true, true};
    }
    void cancel() override;

  private:
    void get(const QString &path, const QUrlQuery &params, quint64 token,
             const std::function<void(const QJsonObject &)> &onSuccess,
             const std::function<void(const QString &)> &onFailure = {});
    QNetworkAccessManager *m_manager;
    QPointer<QNetworkReply> m_reply;
    QUrl m_baseUrl;
    struct Resolution {
        QString hash;
        QString accessKey;
        qint64 audioId = 0;
    };
    QHash<QString, Resolution> m_resolutions;
    quint64 m_handle = 0;
};

} // namespace nekotune
