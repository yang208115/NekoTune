#include "lyrics/lyrics_service.h"
#include "lyrics/lrc_parser.h"
#include "lyrics/kugou_provider.h"
#include "lyrics/lrclib_provider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <algorithm>

namespace nekotune {

LyricsService::LyricsService(LyricsProvider *provider, const QString &cacheDirectory, QObject *parent,
                             KugouProvider *kugouProvider)
    : QObject(parent), m_cache(cacheDirectory), m_lrclibProvider(provider ? provider : new LrclibProvider(this)),
      m_kugouProvider(kugouProvider ? kugouProvider : new KugouProvider(this)), m_provider(m_lrclibProvider)
{
    for (auto *source : {m_lrclibProvider, static_cast<LyricsProvider *>(m_kugouProvider)}) {
        connect(source, &LyricsProvider::completed, this, &LyricsService::completed);
        connect(source, &LyricsProvider::failed, this, &LyricsService::failed);
    }
    connect(m_kugouProvider, &KugouProvider::resolved, this, [this](quint64 token, const LyricsDocument &document) {
        if (token == m_token) apply(document, true);
    });
}

void LyricsService::cancel()
{
    ++m_token;
    m_lrclibProvider->cancel();
    m_kugouProvider->cancel();
    m_candidates.clear();
    m_snapshot.remove(QStringLiteral("candidates"));
}

void LyricsService::clear(quint64 revision)
{
    cancel();
    m_provider = m_lrclibProvider;
    m_query = {};
    m_revision = revision;
    m_snapshot = {};
    publish(QStringLiteral("idle"));
}

void LyricsService::load(const LyricsQuery &query, const QString &path, const QString &customLyrics, quint64 revision,
                         bool metadataReady, bool force)
{
    cancel();
    m_provider = m_lrclibProvider;
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
    if (!force) {
        const auto cached = m_cache.read(query);
        if (cached) {
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

void LyricsService::search(const LyricsQuery &query, quint64 revision, const QString &source)
{
    cancel();
    m_provider = source == QStringLiteral("kugou") ? static_cast<LyricsProvider *>(m_kugouProvider) : m_lrclibProvider;
    if (m_query.trackId != query.trackId)
        m_snapshot = {};
    m_revision = revision;
    m_query = query;
    m_search = true;
    m_manual = true;
    m_snapshot.insert(QStringLiteral("search_source"), source);
    m_snapshot.remove(QStringLiteral("search_stage"));
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
    if (m_offline && m_provider == m_kugouProvider) {
        cancel();
        publish(QStringLiteral("offline"));
        return;
    }
    const auto candidate = m_candidates.at(index);
    cancel();
    if (m_provider == m_kugouProvider) {
        m_snapshot.insert(QStringLiteral("search_stage"), candidate.songResult ? QStringLiteral("lyrics") : QStringLiteral("download"));
        publish(candidate.songResult ? QStringLiteral("searching") : QStringLiteral("loading"));
        m_kugouProvider->choose(candidate, m_token);
    } else {
        apply(candidate.document, true);
    }
}

void LyricsService::setOffline(bool offline)
{
    m_offline = offline;
    const QString state = m_snapshot.value(QStringLiteral("state")).toString();
    if (offline && (state == QStringLiteral("loading") || state == QStringLiteral("searching") ||
                    state == QStringLiteral("candidates"))) {
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
    if (m_provider == m_kugouProvider) {
        if (m_candidates.isEmpty()) {
            publish(QStringLiteral("not_found"));
            return;
        }
        m_snapshot.insert(QStringLiteral("search_stage"), m_candidates.first().songResult ? QStringLiteral("songs") : QStringLiteral("lyrics"));
        QJsonArray array;
        for (const auto &candidate : m_candidates) array.append(candidate.toJson());
        m_snapshot.insert(QStringLiteral("candidates"), array);
        publish(QStringLiteral("candidates"));
        return;
    }
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
    m_snapshot.remove(QStringLiteral("candidates"));
    m_snapshot.insert(QStringLiteral("document"), document.toJson());
    const bool cacheFailed = cache && !m_cache.write(m_query, document);
    m_snapshot.insert(QStringLiteral("cache_warning"), cacheFailed);
    publish(document.instrumental ? QStringLiteral("instrumental") : QStringLiteral("ready"));
}

} // namespace nekotune
