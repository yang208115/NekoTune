#include "infrastructure/lyrics/lyrics_cache.h"
#include "app_paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace nekotune {

LyricsCache::LyricsCache(const QString &directory)
    : m_directory(directory.isEmpty()
                      ? AppPaths::configFile("lyrics-cache")
                      : directory) {}

QString LyricsCache::directory() const { return m_directory; }

QString LyricsCache::keyFor(const LyricsQuery &query) const {
    // Once audio identity is known, renames and metadata edits must not orphan a manually selected lyric.
    const QJsonArray identity =
        query.trackId.isEmpty()
            ? QJsonArray{query.title.simplified().toCaseFolded(), query.artist.simplified().toCaseFolded(),
                         query.album.simplified().toCaseFolded(), query.durationMs}
            : QJsonArray{QStringLiteral("audio-sha256"), query.trackId};
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex());
}

// Treat caches as untrusted recoverable data, not authoritative sidecars.
// Validate schema/key/types before constructing a document value.
// Version one lacks KRC; version two can retain word-timed content.
// Document validation filters unusable timing instead of exposing it.
// An unusable cache is a miss so normal lookup can continue.
// Legacy ASR files are retained while their contents are excluded.
std::optional<LyricsDocument> LyricsCache::read(const LyricsQuery &query) const {
    QFile file(QDir(m_directory).filePath(keyFor(query) + QStringLiteral(".json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024)
        return std::nullopt;
    const auto json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject())
        return std::nullopt;
    const auto object = json.object();
    const int version = object.value(QStringLiteral("version")).toInt();
    if ((version != 1 && version != 2) || object.value(QStringLiteral("key")).toString() != keyFor(query) ||
        !object.value(QStringLiteral("source")).isString() ||
        !object.value(QStringLiteral("synced_lyrics")).isString() ||
        !object.value(QStringLiteral("plain_lyrics")).isString() ||
        !object.value(QStringLiteral("instrumental")).isBool())
        return std::nullopt;
    LyricsDocument result;
    result.source = object.value(QStringLiteral("source")).toString();
    result.coverUrl = object.value(QStringLiteral("cover_url")).toString();
    // Retired ASR entries remain on disk for compatibility, but cannot become active lyrics again.
    if (result.source == QStringLiteral("aliyun_asr"))
        return std::nullopt;
    result.syncedLyrics = object.value(QStringLiteral("synced_lyrics")).toString();
    if (version == 2)
        result.krcLyrics = object.value(QStringLiteral("krc_lyrics")).toString();
    result.plainLyrics = object.value(QStringLiteral("plain_lyrics")).toString();
    result.instrumental = object.value(QStringLiteral("instrumental")).toBool();
    result.providerId = object.value(QStringLiteral("provider_id")).toInteger();
    result.matched = {object.value(QStringLiteral("matched_title")).toString(),
                      object.value(QStringLiteral("matched_artist")).toString(),
                      object.value(QStringLiteral("matched_album")).toString(),
                      object.value(QStringLiteral("matched_duration")).toInteger()};
    result.validate();
    if (result.isEmpty())
        return std::nullopt;
    return result;
}

// Store the selected result rather than a list of uncertain candidates.
// Instrumental results are valid even without lyric text.
// Timed documents avoid persisting redundant plain-text alternatives.
// The matched metadata records the provider version actually selected.
// Atomic commit makes an interrupted write recoverable as the old cache.
bool LyricsCache::write(const LyricsQuery &query, const LyricsDocument &document) const {
    if (document.isEmpty() || !QDir().mkpath(m_directory))
        return false;
    QSaveFile file(QDir(m_directory).filePath(keyFor(query) + QStringLiteral(".json")));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QJsonObject object{
        {QStringLiteral("version"), 2},
        {QStringLiteral("key"), keyFor(query)},
        {QStringLiteral("source"), document.source},
        {QStringLiteral("cover_url"), document.coverUrl},
        {QStringLiteral("synced_lyrics"), document.syncedLyrics},
        {QStringLiteral("krc_lyrics"), document.krcLyrics},
        {QStringLiteral("plain_lyrics"), document.isSynced() ? QString() : document.plainLyrics},
        {QStringLiteral("instrumental"), document.instrumental},
        {QStringLiteral("provider_id"), document.providerId},
        {QStringLiteral("matched_title"), document.matched.title},
        {QStringLiteral("matched_artist"), document.matched.artist},
        {QStringLiteral("matched_album"), document.matched.album},
        {QStringLiteral("matched_duration"), document.matched.durationMs}};
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}

} // namespace nekotune
