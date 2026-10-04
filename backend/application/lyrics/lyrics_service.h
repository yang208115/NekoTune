#pragma once

#include "domain/lyrics/lyrics_provider.h"
#include "domain/lyrics/lyrics_storage.h"
#include <QHash>
#include <QObject>
#include <memory>

namespace nekotune {

/// Runs on the lyrics thread; local/custom/cache sources precede provider lookup.
class LyricsService final : public QObject {
    Q_OBJECT
  public:
    explicit LyricsService(const QVector<LyricsProvider *> &providers,
                           std::unique_ptr<ILyricsStorage> storage, QObject *parent = nullptr);
    QVector<LyricsSource> sources() const;
    void addProvider(LyricsProvider *provider, bool takeOwnership = true);
    void removeProvider(const QString &id);
    bool offline() const { return m_offline; }
    void shutdown() { cancel(); }
    /// force skips custom lyrics and cache, but still respects authoritative local sidecars.
    /// metadataReady gates online auto-matching; revision belongs to the controller's UI request.
    /// @param query Display metadata and stable audio identity for source selection.
    /// @param path Local audio path used to locate adjacent authoritative sidecars.
    /// @param customLyrics User-authored library lyrics, independent of provider cache.
    /// @param revision Request identity echoed in snapshots and asset notifications.
    /// @param metadataReady Whether automatic online matching has sufficient decoder metadata.
    /// @param force Bypass custom/cache content while retaining local-sidecar precedence.
    /// Completion is published asynchronously; callers observe changed rather than a return value.
    void load(const LyricsQuery &query, const QString &path, const QString &customLyrics, quint64 revision,
              bool metadataReady, bool force = false);
    /// @param source Registered provider ID, not its localized display name.
    /// @param revision Current controller request identity for the resulting candidate list.
    /// Manual search preserves the selected document while replacing search candidates.
    /// Offline mode and unavailable providers publish an error state without network admission.
    void search(const LyricsQuery &query, quint64 revision, const QString &source = QStringLiteral("lrclib"));
    /// @param index Index in the latest published candidate list.
    /// @param revision Must match the active request before selection is applied.
    /// A staged provider may publish another list before producing a document.
    /// Out-of-range and stale selections must not alter the visible lyrics.
    void select(int index, quint64 revision);
    void clear(quint64 revision);
    void setOffline(bool offline);

    static QString normalizeForMatch(const QString &value);
    /// Score title, artist and duration compatibility using normalized display metadata.
    /// The score ranks candidates; it does not by itself authorize automatic selection.
    /// Unknown duration is treated separately from an actual duration mismatch.
    static double matchScore(const LyricsQuery &query, const LyricsCandidate &candidate);
    static QVector<LyricsCandidate> rankCandidates(const LyricsQuery &query,
                                                   QVector<LyricsCandidate> candidates);
    /// Decide whether the best ranked result is strong and distinct enough for auto-selection.
    /// Ambiguous alternatives remain candidates for explicit user choice.
    /// Call with rankCandidates output so the top-result and runner-up comparison is meaningful.
    static bool confident(const LyricsQuery &query, const QVector<LyricsCandidate> &ranked);

  signals:
    void sourcesChanged(const QVector<nekotune::LyricsSource> &sources);
    void changed(const nekotune::LyricsSnapshot &snapshot);
    void assetsReady(const nekotune::LyricsQuery &query, const nekotune::LyricsDocument &document,
                     quint64 revision);

  private:
    void completed(quint64 token, const QVector<LyricsCandidate> &candidates);
    void failed(quint64 token, const QString &kind);
    void publish(const QString &state, const QString &error = {});
    void apply(const LyricsDocument &document, bool cache);
    void cancel();

    std::unique_ptr<ILyricsStorage> m_storage;
    QHash<QString, LyricsProvider *> m_providers;
    LyricsProvider *m_defaultProvider = nullptr;
    LyricsProvider *m_provider;
    LyricsQuery m_query;
    // Token identifies a provider attempt; one UI revision can include lookup, search and resolution.
    quint64 m_token = 0;
    quint64 m_revision = 0;
    bool m_search = false;
    bool m_manual = false;
    bool m_offline = false;
    QVector<LyricsCandidate> m_candidates;
    LyricsSnapshot m_snapshot;
};

} // namespace nekotune
