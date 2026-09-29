#include "lyrics/lyrics_types.h"
#include "lyrics/asr_parser.h"
#include "lyrics/lrc_parser.h"

#include <QJsonArray>

namespace nekotune {

void LyricsDocument::validate()
{
    if (!LrcParser::looksLikeLrc(syncedLyrics))
        syncedLyrics.clear();
    if (AsrParser::parse(asrLyrics.toUtf8()).isEmpty())
        asrLyrics.clear();
    if (instrumental) {
        asrLyrics.clear();
        syncedLyrics.clear();
        plainLyrics.clear();
    }
}

QJsonObject LyricsDocument::toJson() const
{
    const auto lines = asrLyrics.isEmpty() ? LrcParser::parse(syncedLyrics) : AsrParser::parse(asrLyrics.toUtf8());
    return {{QStringLiteral("source"), source},
            {QStringLiteral("synced"), isSynced()},
            {QStringLiteral("instrumental"), instrumental},
            {QStringLiteral("plain_text"), plainLyrics},
            {QStringLiteral("lines"), LrcParser::toJson(lines)}};
}

QJsonObject LyricsCandidate::toJson() const
{
    return {{QStringLiteral("id"), document.providerId},
            {QStringLiteral("title"), document.matched.title},
            {QStringLiteral("artist"), document.matched.artist},
            {QStringLiteral("album"), document.matched.album},
            {QStringLiteral("duration"), document.matched.durationMs},
            {QStringLiteral("score"), score},
            {QStringLiteral("synced"), document.isSynced()},
            {QStringLiteral("instrumental"), document.instrumental}};
}

} // namespace nekotune
