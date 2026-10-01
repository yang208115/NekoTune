#include "application/lyrics_service.h"
#include "domain/krc_parser.h"
#include "domain/lrc_parser.h"

#include <algorithm>

namespace nekotune {

LyricsService::LyricsService(const QVector<LyricsProvider *> &providers,
                             std::unique_ptr<ILyricsStorage> storage, QObject *parent)
    : QObject(parent), m_storage(std::move(storage)), m_provider(nullptr) {
    for (auto *source : providers) {
        const auto id = source->descriptor().id;
        Q_ASSERT(!id.isEmpty() && !m_providers.contains(id));
        m_providers.insert(id, source);
        connect(source, &LyricsProvider::completed, this, &LyricsService::completed);
        connect(source, &LyricsProvider::failed, this, &LyricsService::failed);
        connect(source, &LyricsProvider::resolved, this,
                [this](quint64 token, const LyricsDocument &document) {
                    if (token == m_token)
                        apply(document, true);
                });
    }
    m_defaultProvider = m_providers.value(QStringLiteral("lrclib"));
    m_provider = m_defaultProvider;
}
QVector<LyricsSource> LyricsService::sources() const {
    QVector<LyricsSource> result;
    for (auto *source : m_providers)
        result.append(source->descriptor());
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.id > b.id; });
    return result;
}

void LyricsService::cancel() {
    ++m_token;
    for (auto *source : m_providers)
        source->cancel();
    m_candidates.clear();
    m_snapshot.candidates.clear();
}

void LyricsService::clear(quint64 revision) {
    cancel();
    m_provider = m_defaultProvider;
    m_query = {};
    m_revision = revision;
    m_snapshot = {};
    publish(QStringLiteral("idle"));
}

void LyricsService::load(const LyricsQuery &query, const QString &path, const QString &customLyrics,
                         quint64 revision, bool metadataReady, bool force) {
    cancel();
    m_provider = m_defaultProvider;
    m_revision = revision;
    m_query = query;
    m_manual = false;
    m_search = false;
    m_snapshot = {};
    publish(QStringLiteral("loading"));
    const auto local = m_storage->readLocal(query, path);
    if (!local) {
        publish(QStringLiteral("error"), local.error().message);
        return;
    }
    if (local.value()) {
        apply(*local.value(), false);
        return;
    }
    if (!force) {
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
        const auto cached = m_storage->readCache(query);
        if (cached) {
            m_snapshot.cached = true;
            apply(*cached, false);
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
    if (m_provider)
        m_provider->request(query, m_token, false);
    else
        publish(QStringLiteral("not_found"));
}

void LyricsService::search(const LyricsQuery &query, quint64 revision, const QString &source) {
    cancel();
    m_provider = m_providers.value(source);
    if (!m_provider) {
        publish(QStringLiteral("error"), QStringLiteral("unknown_source"));
        return;
    }
    if (m_query.trackId != query.trackId)
        m_snapshot = {};
    m_revision = revision;
    m_query = query;
    m_search = true;
    m_manual = true;
    m_snapshot.searchSource = source;
    m_snapshot.searchStage.clear();
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

void LyricsService::select(int index, quint64 revision) {
    if (revision != m_revision || index < 0 || index >= m_candidates.size())
        return;
    if (m_offline && m_provider && m_provider->descriptor().staged) {
        cancel();
        publish(QStringLiteral("offline"));
        return;
    }
    const auto candidate = m_candidates.at(index);
    cancel();
    if (m_provider && m_provider->descriptor().staged) {
        m_snapshot.searchStage = candidate.songResult ? QStringLiteral("lyrics") : QStringLiteral("download");
        publish(candidate.songResult ? QStringLiteral("searching") : QStringLiteral("loading"));
        m_provider->choose(candidate, m_token);
    } else {
        m_provider->choose(candidate, m_token);
    }
}

void LyricsService::setOffline(bool offline) {
    m_offline = offline;
    const QString state = m_snapshot.state;
    if (offline && (state == QStringLiteral("loading") || state == QStringLiteral("searching") ||
                    state == QStringLiteral("candidates"))) {
        cancel();
        publish(QStringLiteral("offline"));
    } else {
        m_snapshot.offline = offline;
        emit changed(m_snapshot);
    }
}

QString LyricsService::normalizeForMatch(const QString &value) {
    return value.normalized(QString::NormalizationForm_C).simplified().toCaseFolded();
}

double LyricsService::matchScore(const LyricsQuery &query, const LyricsCandidate &candidate) {
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

QVector<LyricsCandidate> LyricsService::rankCandidates(const LyricsQuery &query,
                                                       QVector<LyricsCandidate> candidates) {
    for (auto &candidate : candidates)
        candidate.score = matchScore(query, candidate);
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.document.isSynced() && !b.document.isSynced();
    });
    return candidates;
}

bool LyricsService::confident(const LyricsQuery &query, const QVector<LyricsCandidate> &ranked) {
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

void LyricsService::completed(quint64 token, const QVector<LyricsCandidate> &candidates) {
    if (token != m_token)
        return;
    m_candidates = rankCandidates(m_query, candidates);
    if (m_provider && m_provider->descriptor().staged) {
        if (m_candidates.isEmpty()) {
            publish(QStringLiteral("not_found"));
            return;
        }
        m_snapshot.searchStage =
            m_candidates.first().songResult ? QStringLiteral("songs") : QStringLiteral("lyrics");
        m_snapshot.candidates = m_candidates;
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
        m_snapshot.candidates = m_candidates;
        publish(QStringLiteral("candidates"));
    }
}

void LyricsService::failed(quint64 token, const QString &kind) {
    if (token != m_token)
        return;
    if (kind == QStringLiteral("not_found") && !m_search) {
        m_search = true;
        m_provider->request(m_query, ++m_token, true);
        return;
    }
    publish(kind == QStringLiteral("not_found") ? kind : QStringLiteral("error"), kind);
}

void LyricsService::publish(const QString &state, const QString &error) {
    m_snapshot.trackId = m_query.trackId;
    m_snapshot.revision = m_revision;
    m_snapshot.state = state;
    m_snapshot.error = error;
    m_snapshot.offline = m_offline;
    emit changed(m_snapshot);
}

void LyricsService::apply(const LyricsDocument &document, bool cache) {
    m_snapshot.candidates.clear();
    m_snapshot.document = document;
    const bool cacheFailed = cache && !m_storage->writeCache(m_query, document);
    m_snapshot.cacheWarning = cacheFailed;
    publish(document.instrumental ? QStringLiteral("instrumental") : QStringLiteral("ready"));
}

} // namespace nekotune
