#pragma once

#include "domain/lyrics/lyrics_provider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QUrlQuery>
#include <functional>

namespace nekotune {

/// Manual resolution has song, lyric-candidate and document stages.
/// Song search expands grouped provider versions into distinct choices.
/// Resolver handles retain hashes/access data inside this provider.
/// Selecting a song searches lyrics for that chosen version.
/// Selecting a lyric requests KRC first and falls back to LRC.
/// Cancel releases network work without erasing unrelated local files.
class KugouProvider final : public LyricsProvider {
    Q_OBJECT
  public:
    explicit KugouProvider(
        QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr,
        const QUrl &baseUrl = {});
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
    bool m_useSettings = true;
    bool configurationAvailable() const;
    struct Resolution {
        QString hash;
        QString accessKey;
        qint64 audioId = 0;
    };
    QHash<QString, Resolution> m_resolutions;
    quint64 m_handle = 0;
};

} // namespace nekotune
