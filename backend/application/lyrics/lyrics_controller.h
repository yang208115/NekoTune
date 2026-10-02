#pragma once
#include "application/lyrics/lyrics_service.h"
#include "application/playback/player_engine.h"
#include <QThread>
namespace nekotune {
/// Bridges player-thread snapshots to a lyrics worker; revisions isolate successive user requests.
class LyricsController final : public QObject {
    Q_OBJECT
  public:
    LyricsController(PlayerEngine &player, LyricsService *service);
    ~LyricsController() override;
    const LyricsSnapshot &snapshot() const { return m_snapshot; }
    const QVector<LyricsSource> &sources() const { return m_sources; }
    /// @param trackId Audio hash that must still be current when refresh is admitted.
    /// Success means a new forced load was queued, not that a provider already returned lyrics.
    /// The existing local sidecar remains authoritative even during forced refresh.
    Result<void> refresh(const QString &trackId);
    /// @param fields Optional title/artist replacements for this search only.
    /// @param album Null preserves the current album; a supplied empty value clears that constraint.
    /// @param source Registered searchable provider ID validated before worker dispatch.
    /// Cancel pending decoder metadata refresh so it cannot supersede the manual search.
    /// Search metadata is not persisted into the library.
    Result<void> search(const QString &trackId, const MetadataPatch &fields, const QString &album,
                        const QString &source);
    /// @param trackId Must match the current audio hash.
    /// @param revision Candidate-list revision observed by the frontend.
    /// @param index Position inside that revision's candidate snapshot.
    /// Validate here and again on the worker because queued execution can race a later request.
    Result<void> select(const QString &trackId, quint64 revision, int index);
    void setOffline(bool offline);
    /// Disconnect player-triggered loads before cancelling the worker's active request.
    /// Cancellation is invoked while the worker event loop is still running.
    /// Join before destruction so queued network work cannot outlive its owning service.
    void shutdown();
  signals:
    void changed(const nekotune::LyricsSnapshot &snapshot);
    void assetsReady(const QString &trackId, const nekotune::LyricsDocument &document, quint64 revision);

  private:
    LyricsQuery query() const;
    void load(bool metadataReady, bool force = false);
    PlayerEngine &m_player;
    LyricsService *m_service;
    QThread m_thread;
    QVector<LyricsSource> m_sources;
    LyricsSnapshot m_snapshot;
    quint64 m_revision = 0;
    bool m_ready = false;
};
} // namespace nekotune
