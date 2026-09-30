#pragma once

#include "lyrics/lyrics_provider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QUrlQuery>
#include <functional>

namespace nekotune {

class KugouProvider final : public LyricsProvider {
    Q_OBJECT
  public:
    explicit KugouProvider(QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr,
                           const QUrl &baseUrl = QUrl(QStringLiteral("https://kugou-lyrics-api.lyuy.workers.dev")));
    ~KugouProvider() override;
    void request(const LyricsQuery &query, quint64 token, bool search) override;
    void choose(const LyricsCandidate &candidate, quint64 token);
    void cancel() override;

  signals:
    void resolved(quint64 token, const LyricsDocument &document);

  private:
    void get(const QString &path, const QUrlQuery &params, quint64 token,
             const std::function<void(const QJsonObject &)> &onSuccess);
    QNetworkAccessManager *m_manager;
    QPointer<QNetworkReply> m_reply;
    QUrl m_baseUrl;
};

} // namespace nekotune
