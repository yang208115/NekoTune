#include "lyrics/lyrics_service.h"
#include "lyrics/asr_parser.h"
#include "lyrics/lrc_parser.h"
#include "lyrics/lrclib_provider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <algorithm>

namespace nekotune {

LyricsService::LyricsService(LyricsProvider *provider, const QString &cacheDirectory, QObject *parent)
    : QObject(parent), m_cache(cacheDirectory), m_provider(provider ? provider : new LrclibProvider(this))
{
    connect(m_provider, &LyricsProvider::completed, this, &LyricsService::completed);
    connect(m_provider, &LyricsProvider::failed, this, &LyricsService::failed);
}

void LyricsService::cancel()
{
    ++m_token;
    m_provider->cancel();
    m_candidates.clear();
    m_snapshot.remove(QStringLiteral("candidates"));
}

void LyricsService::clear(quint64 revision)
{
    cancel();
    m_query = {};
    m_revision = revision;
    m_snapshot = {};
    publish(QStringLiteral("idle"));
}

void LyricsService::load(const LyricsQuery &query, const QString &path, const QString &customLyrics, quint64 revision,
                         bool metadataReady, bool force, const QByteArray &storedAsr)
{
    cancel();
    m_revision = revision;
    m_query = query;
    m_manual = false;
    m_search = false;
    m_snapshot = {};
    publish(QStringLiteral("loading"));
    const QFileInfo audio(path);
    QString localPath = audio.absoluteDir().filePath(audio.completeBaseName() + QStringLiteral(".lrc"));
    const auto sidecars = audio.absoluteDir().entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto &sidecar : sidecars) {
        if (sidecar.suffix().compare(QStringLiteral("lrc"), Qt::CaseInsensitive) == 0
            && sidecar.completeBaseName().compare(audio.completeBaseName(), Qt::CaseInsensitive) == 0) {
            localPath = sidecar.absoluteFilePath();
            break;
        }
    }
    QFile local(localPath);
    // Collect available alternatives without changing playback priority, requesting
    // recognition, or exposing the raw transcription metadata to clients.
    LyricsDocument customDocument;
    customDocument.source = QStringLiteral("custom");
    customDocument.syncedLyrics = customLyrics;
    customDocument.validate();
    rememberComparison(customDocument);
    const auto cached = m_cache.read(query);
    if (cached)
        rememberComparison(*cached);
    LyricsDocument asrDocument;
    asrDocument.source = QStringLiteral("aliyun_asr");
    if (storedAsr.size() <= 2 * 1024 * 1024) {
        asrDocument.asrLyrics = QString::fromUtf8(storedAsr);
        rememberComparison(asrDocument);
    }
    if (local.exists()) {
        const auto asrName = audio.completeBaseName() + QStringLiteral(".asr.json");
        for (const auto &sidecar : sidecars) {
            if (sidecar.fileName().compare(asrName, Qt::CaseInsensitive) != 0)
                continue;
            QFile asrFile(sidecar.absoluteFilePath());
            if (asrFile.open(QIODevice::ReadOnly) && asrFile.size() <= 2 * 1024 * 1024) {
                asrDocument.asrLyrics = QString::fromUtf8(asrFile.read(2 * 1024 * 1024 + 1));
                rememberComparison(asrDocument);
            }
            break;
        }
    }
    if (local.exists()) {
        // Presence of a sidecar is authoritative, including malformed or unreadable
        // files.
        if (!local.open(QIODevice::ReadOnly) || local.size() > 2 * 1024 * 1024) {
            publish(QStringLiteral("error"), QStringLiteral("local_read"));
            return;
        }
        LyricsDocument document;
        document.source = QStringLiteral("local");
        const QString localText = QString::fromUtf8(local.readAll());
        if (LrcParser::looksLikeLrc(localText))
            document.syncedLyrics = localText;
        else
            document.plainLyrics = localText;
        document.matched = query;
        document.validate();
        if (document.isEmpty())
            publish(QStringLiteral("error"), QStringLiteral("invalid_local"));
        else
            apply(document, false);
        return;
    }
    const auto asrName = audio.completeBaseName() + QStringLiteral(".asr.json");
    for (const auto &sidecar : sidecars) {
        if (sidecar.fileName().compare(asrName, Qt::CaseInsensitive) == 0) {
            importAsr(query, sidecar.absoluteFilePath(), revision);
            return;
        }
    }
    if (!force) {
        if (!storedAsr.isEmpty()) {
            applyAsr(query, storedAsr, revision, false);
            return;
        }
        if (cached) {
            if (!cached->asrLyrics.isEmpty())
                emit transcriptionReady(query.trackId, cached->asrLyrics.toUtf8());
            m_snapshot.insert(QStringLiteral("cached"), true);
            apply(*cached, false);
            return;
        }
        if (!customLyrics.trimmed().isEmpty()) {
            LyricsDocument document;
            document.source = QStringLiteral("custom");
            if (LrcParser::looksLikeLrc(customLyrics))
                document.syncedLyrics = customLyrics;
            else
                document.plainLyrics = customLyrics;
            document.matched = query;
            document.validate();
            apply(document, false);
            return;
        }
    }
    if (m_offline) {
        publish(QStringLiteral("offline"));
        return;
    }
    if (!metadataReady) {
        publish(QStringLiteral("waiting_metadata"));
        return;
    }
    if (query.title.trimmed().isEmpty() || query.artist.trimmed().isEmpty()) {
        publish(QStringLiteral("needs_metadata"));
        return;
    }
    m_provider->request(query, m_token, false);
}

void LyricsService::importAsr(const LyricsQuery &query, const QString &path, quint64 revision)
{
    cancel();
    if (m_query.trackId != query.trackId)
        m_snapshot = {};
    m_query = query;
    m_revision = revision;
    QFile file(path);
    if (!QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) {
        publish(QStringLiteral("error"), QStringLiteral("local_read"));
        return;
    }
    applyAsr(query, file.read(2 * 1024 * 1024 + 1), revision);
}

void LyricsService::applyAsr(const LyricsQuery &query, const QByteArray &json, quint64 revision, bool persist)
{
    cancel();
    if (m_query.trackId != query.trackId)
        m_snapshot = {};
    m_query = query;
    m_revision = revision;
    const auto lines = json.size() <= 2 * 1024 * 1024 ? AsrParser::parse(json) : QVector<LyricLine>{};
    if (lines.isEmpty()) {
        publish(QStringLiteral("error"), QStringLiteral("invalid_asr"));
        return;
    }
    LyricsDocument document;
    document.source = QStringLiteral("aliyun_asr");
    document.matched = query;
    document.asrLyrics = AsrParser::serialize(lines);
    m_snapshot.remove(QStringLiteral("cached"));
    if (persist)
        emit transcriptionReady(query.trackId, json);
    m_snapshot.insert(QStringLiteral("stored"), !persist);
    apply(document, false);
}

void LyricsService::search(const LyricsQuery &query, quint64 revision)
{
    cancel();
    if (m_query.trackId != query.trackId)
        m_snapshot = {};
    m_revision = revision;
    m_query = query;
    m_search = true;
    m_manual = true;
    if (m_offline) {
        publish(QStringLiteral("offline"));
        return;
    }
    if (query.title.trimmed().isEmpty()) {
        publish(QStringLiteral("needs_metadata"));
        return;
    }
    publish(QStringLiteral("searching"));
    m_provider->request(query, m_token, true);
}

void LyricsService::select(int index, quint64 revision)
{
    if (revision != m_revision || index < 0 || index >= m_candidates.size())
        return;
    const auto document = m_candidates.at(index).document;
    cancel();
    apply(document, true);
}

void LyricsService::setOffline(bool offline)
{
    m_offline = offline;
    const QString state = m_snapshot.value(QStringLiteral("state")).toString();
    if (offline && (state == QStringLiteral("loading") || state == QStringLiteral("searching"))) {
        cancel();
        publish(QStringLiteral("offline"));
    } else {
        m_snapshot.insert(QStringLiteral("offline"), offline);
        emit changed(m_snapshot);
    }
}

QString LyricsService::normalizeForMatch(const QString &value)
{
    return value.normalized(QString::NormalizationForm_C).simplified().toCaseFolded();
}

double LyricsService::matchScore(const LyricsQuery &query, const LyricsCandidate &candidate)
{
    const auto &match = candidate.document.matched;
    double score = 0;
    if (!query.title.isEmpty() && normalizeForMatch(query.title) == normalizeForMatch(match.title))
        score += 50;
    if (!query.artist.isEmpty() && normalizeForMatch(query.artist) == normalizeForMatch(match.artist))
        score += 25;
    if (!query.album.isEmpty() && normalizeForMatch(query.album) == normalizeForMatch(match.album))
        score += 15;
    if (query.durationMs > 0 && match.durationMs > 0) {
        const auto difference = qAbs(query.durationMs - match.durationMs);
        if (difference <= 2000)
            score += 10;
        else if (difference <= 5000)
            score += 3;
        else
            score -= 30;
    }
    return score;
}

QVector<LyricsCandidate> LyricsService::rankCandidates(const LyricsQuery &query, QVector<LyricsCandidate> candidates)
{
    for (auto &candidate : candidates)
        candidate.score = matchScore(query, candidate);
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.document.isSynced() && !b.document.isSynced();
    });
    return candidates;
}

bool LyricsService::confident(const LyricsQuery &query, const QVector<LyricsCandidate> &ranked)
{
    if (ranked.isEmpty() || query.title.isEmpty() || query.artist.isEmpty() || query.durationMs <= 0)
        return false;
    const auto &best = ranked.first().document;
    const auto &match = best.matched;
    if (normalizeForMatch(query.title) != normalizeForMatch(match.title) ||
        normalizeForMatch(query.artist) != normalizeForMatch(match.artist) ||
        (!query.album.isEmpty() && normalizeForMatch(query.album) != normalizeForMatch(match.album)) ||
        match.durationMs <= 0 || qAbs(query.durationMs - match.durationMs) > 2000)
        return false;
    return ranked.size() == 1 || ranked.at(0).score - ranked.at(1).score >= 10;
}

void LyricsService::completed(quint64 token, const QVector<LyricsCandidate> &candidates)
{
    if (token != m_token)
        return;
    m_candidates = rankCandidates(m_query, candidates);
    if (!m_manual && confident(m_query, m_candidates)) {
        apply(m_candidates.first().document, true);
    } else if (!m_search) {
        m_search = true;
        m_provider->request(m_query, ++m_token, true);
    } else if (m_candidates.isEmpty()) {
        publish(QStringLiteral("not_found"));
    } else {
        QJsonArray array;
        for (const auto &candidate : m_candidates)
            array.append(candidate.toJson());
        m_snapshot.insert(QStringLiteral("candidates"), array);
        publish(QStringLiteral("candidates"));
    }
}

void LyricsService::failed(quint64 token, const QString &kind)
{
    if (token != m_token)
        return;
    if (kind == QStringLiteral("not_found") && !m_search) {
        m_search = true;
        m_provider->request(m_query, ++m_token, true);
        return;
    }
    publish(kind == QStringLiteral("not_found") ? kind : QStringLiteral("error"), kind);
}

void LyricsService::publish(const QString &state, const QString &error)
{
    m_snapshot.insert(QStringLiteral("track_id"), m_query.trackId);
    m_snapshot.insert(QStringLiteral("revision"), QString::number(m_revision));
    m_snapshot.insert(QStringLiteral("state"), state);
    m_snapshot.insert(QStringLiteral("error"), error);
    m_snapshot.insert(QStringLiteral("offline"), m_offline);
    emit changed(m_snapshot);
}

void LyricsService::apply(const LyricsDocument &document, bool cache)
{
    rememberComparison(document);
    m_snapshot.remove(QStringLiteral("candidates"));
    m_snapshot.insert(QStringLiteral("document"), document.toJson());
    const bool cacheFailed = cache && !m_cache.write(m_query, document);
    m_snapshot.insert(QStringLiteral("cache_warning"), cacheFailed);
    publish(document.instrumental ? QStringLiteral("instrumental") : QStringLiteral("ready"));
}

void LyricsService::rememberComparison(const LyricsDocument &document)
{
    auto comparison = m_snapshot.value(QStringLiteral("comparison")).toObject();
    // Parse each format independently even if a cache entry contains both.
    auto lrc = document;
    lrc.asrLyrics.clear();
    if (!LrcParser::parse(lrc.syncedLyrics).isEmpty())
        comparison.insert(QStringLiteral("lrc"), lrc.toJson());
    if (!AsrParser::parse(document.asrLyrics.toUtf8()).isEmpty())
        comparison.insert(QStringLiteral("asr"), document.toJson());
    m_snapshot.insert(QStringLiteral("comparison"), comparison);
}

} // namespace nekotune
