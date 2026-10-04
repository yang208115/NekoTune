#include "infrastructure/extensions/extension_lyrics_provider.h"
#include <QJsonDocument>
#include <QJsonObject>
namespace nekotune {
LyricsDocument ExtensionLyricsProvider::document(const QVariantMap &value) const {
    LyricsDocument result;
    result.source = m_id;
    result.matched = m_query;
    result.syncedLyrics = value.value("synced_lyrics").toString();
    result.plainLyrics = value.value("plain_lyrics").toString();
    result.krcLyrics = value.value("krc_lyrics").toString();
    result.coverUrl = value.value("cover_url").toString();
    result.instrumental = value.value("instrumental").toBool();
    result.validate();
    return result;
}
void ExtensionLyricsProvider::request(const LyricsQuery &query, quint64 token, bool) {
    m_query = query;
    call("search",
         {{"title", query.title},
          {"artist", query.artist},
          {"album", query.album},
          {"duration_ms", query.durationMs},
          {"track_id", query.trackId}},
         token, true);
}
void ExtensionLyricsProvider::choose(const LyricsCandidate &candidate, quint64 token) {
    const auto params = QJsonDocument::fromJson(candidate.resolverHandle.toUtf8()).object().toVariantMap();
    call("resolve", params, token, false);
}
void ExtensionLyricsProvider::call(const QString &operation, const QVariantMap &params, quint64 token,
                                   bool candidates) {
    const auto generation = ++m_generation;
    QPointer<ExtensionLyricsProvider> guard(this);
    if (!m_backend) {
        emit failed(token, "Extension source unavailable");
        return;
    }
    QMetaObject::invokeMethod(
        m_backend,
        [backend = m_backend, guard, source = m_id, operation, params, token, candidates, generation] {
            if (!backend || !guard)
                return;
            backend->request(
                "extensions.provider_call",
                {{"source", source}, {"kind", "lyrics"}, {"operation", operation}, {"params", params}},
                [guard, token, candidates, generation](Result<QVariantMap> result) {
                    if (!guard)
                        return;
                    QMetaObject::invokeMethod(
                        guard,
                        [guard, token, candidates, generation, result] {
                            if (!guard || generation != guard->m_generation)
                                return;
                            if (!result) {
                                emit guard->failed(token, result.error().message);
                                return;
                            }
                            if (!candidates && !result.value().contains("candidates")) {
                                emit guard->resolved(token, guard->document(result.value()));
                                return;
                            }
                            QVector<LyricsCandidate> found;
                            for (const auto &value : result.value().value("candidates").toList()) {
                                const auto item = value.toMap();
                                LyricsCandidate candidate;
                                candidate.document = guard->document(item);
                                candidate.score = item.value("score", 1.0).toDouble();
                                candidate.songResult = item.value("song_result").toBool();
                                candidate.resolverHandle =
                                    QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(item))
                                                          .toJson(QJsonDocument::Compact));
                                if (item.contains("title"))
                                    candidate.document.matched.title = item.value("title").toString();
                                if (item.contains("artist"))
                                    candidate.document.matched.artist = item.value("artist").toString();
                                candidate.document.matched.album = item.value("album").toString();
                                candidate.document.matched.durationMs =
                                    item.value("duration_ms").toLongLong();
                                found.append(candidate);
                            }
                            emit guard->completed(token, found);
                        },
                        Qt::QueuedConnection);
                });
        },
        Qt::QueuedConnection);
}
} // namespace nekotune
