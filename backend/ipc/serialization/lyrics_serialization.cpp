#include "ipc/serialization/lyrics_serialization.h"
#include "domain/lyrics/krc_parser.h"
#include "domain/lyrics/lrc_parser.h"
#include <QJsonArray>
namespace nekotune {
namespace {
QJsonArray lyricLinesJson(const QVector<LyricLine> &lines) {
    QJsonArray result;
    for (const auto &line : lines) {
        QJsonObject object{{QStringLiteral("time_ms"), line.timestampMs},
                           {QStringLiteral("text"), line.text}};
        result.append(object);
    }
    return result;
}
QJsonArray krcLinesJson(const QVector<KrcLine> &lines) {
    QJsonArray result;
    for (const auto &line : lines) {
        QJsonArray words;
        for (const auto &word : line.words)
            words.append(QJsonObject{{QStringLiteral("text"), word.text},
                                     {QStringLiteral("offset_ms"), word.offsetMs},
                                     {QStringLiteral("time_ms"), word.timestampMs},
                                     {QStringLiteral("duration_ms"), word.durationMs}});
        result.append(QJsonObject{{QStringLiteral("time_ms"), line.timestampMs},
                                  {QStringLiteral("duration_ms"), line.durationMs},
                                  {QStringLiteral("text"), line.text},
                                  {QStringLiteral("words"), words}});
    }
    return result;
}
} // namespace

QJsonObject toJson(const LyricsDocument &document) {
    const bool krc = !document.krcLyrics.isEmpty();
    const auto lines = krc ? krcLinesJson(KrcParser::parse(document.krcLyrics))
                           : lyricLinesJson(LrcParser::parse(document.syncedLyrics));
    return {{QStringLiteral("source"), document.source},
            {QStringLiteral("format"), krc                                ? QStringLiteral("krc")
                                       : !document.syncedLyrics.isEmpty() ? QStringLiteral("lrc")
                                                                          : QStringLiteral("plain")},
            {QStringLiteral("cover_url"), document.coverUrl},
            {QStringLiteral("synced"), document.isSynced()},
            {QStringLiteral("instrumental"), document.instrumental},
            {QStringLiteral("plain_text"), document.plainLyrics},
            {QStringLiteral("lines"), lines}};
}

QJsonObject toJson(const LyricsCandidate &candidate) {
    return {{QStringLiteral("id"), candidate.document.providerId},
            {QStringLiteral("source"), candidate.document.source},
            {QStringLiteral("cover_url"), candidate.document.coverUrl},
            {QStringLiteral("song_result"), candidate.songResult},
            {QStringLiteral("title"), candidate.document.matched.title},
            {QStringLiteral("artist"), candidate.document.matched.artist},
            {QStringLiteral("album"), candidate.document.matched.album},
            {QStringLiteral("duration"), candidate.document.matched.durationMs},
            {QStringLiteral("score"), candidate.score},
            {QStringLiteral("synced"), candidate.document.isSynced()},
            {QStringLiteral("instrumental"), candidate.document.instrumental}};
}

// Encode revisions as text because JavaScript numbers lose large integers.
// Candidate ordering and revision form one frontend selection contract.
// Only publish optional document/candidate fields when they exist.
// cache_warning describes persistence, not whether display is usable.
// Provider resolver handles are intentionally absent from serialized candidates.
QJsonObject toJson(const LyricsSnapshot &state) {
    QJsonObject value{{"track_id", state.trackId},
                      {"revision", QString::number(state.revision)},
                      {"state", state.state},
                      {"error", state.error},
                      {"offline", state.offline}};
    if (!state.searchSource.isEmpty())
        value.insert("search_source", state.searchSource);
    if (!state.searchStage.isEmpty())
        value.insert("search_stage", state.searchStage);
    if (state.cached)
        value.insert("cached", true);
    if (state.document) {
        value.insert("document", toJson(*state.document));
        value.insert("cache_warning", state.cacheWarning);
    }
    if (!state.candidates.isEmpty()) {
        QJsonArray items;
        for (const auto &item : state.candidates)
            items.append(toJson(item));
        value.insert("candidates", items);
    }
    return value;
}
} // namespace nekotune
