#include "lyrics/lyrics_types.h"
#include "lyrics/lrc_parser.h"

#include <QJsonArray>

namespace nekotune {

void LyricsDocument::validate()
{
    if (!LrcParser::looksLikeLrc(syncedLyrics))
        syncedLyrics.clear();
    if (instrumental) {
        syncedLyrics.clear();
        plainLyrics.clear();
    }
}

QJsonObject LyricsDocument::toJson() const
{
    const auto lines = LrcParser::parse(syncedLyrics);
    return {{QStringLiteral("source"), source},
            {QStringLiteral("cover_url"), coverUrl},
            {QStringLiteral("synced"), isSynced()},
            {QStringLiteral("instrumental"), instrumental},
            {QStringLiteral("plain_text"), plainLyrics},
            {QStringLiteral("lines"), LrcParser::toJson(lines)}};
}

QJsonObject LyricsCandidate::toJson() const
{
    return {{QStringLiteral("id"), document.providerId},
            {QStringLiteral("source"), document.source},
            {QStringLiteral("cover_url"), document.coverUrl},
            {QStringLiteral("song_result"), songResult},
            {QStringLiteral("title"), document.matched.title},
            {QStringLiteral("artist"), document.matched.artist},
            {QStringLiteral("album"), document.matched.album},
            {QStringLiteral("duration"), document.matched.durationMs},
            {QStringLiteral("score"), score},
            {QStringLiteral("synced"), document.isSynced()},
            {QStringLiteral("instrumental"), document.instrumental}};
}

} // namespace nekotune
