#pragma once
#include "application/playback/queue_service.h"
#include "application/playback/playback_order_service.h"
#include "domain/playback/playback_types.h"
#include <QTimer>
namespace nekotune {
/// Coordinates persisted queue selection and playback; storage and decoding stay in adapters.
class PlayerEngine final : public QObject {
    Q_OBJECT
  public:
    PlayerEngine(IPlaybackBackend &backend, QueueService &queue,
                 std::unique_ptr<IShuffleStrategy> strategy = std::make_unique<ShuffleBagStrategy>(),
                 PlaybackMode mode = PlaybackMode::Sequential,
                 PlaybackOrderService::ModeSaver saveMode = {});
    /// Persist a mode before making it authoritative for navigation or UI state.
    /// The injected mode saver may reject the change without consuming shuffle history.
    /// No change notification is emitted when the requested mode is already active.
    Result<void> setPlaybackMode(PlaybackMode mode);
    PlaybackSnapshot snapshot() const;
    /// Resume a loaded source or load the selected persisted occurrence.
    /// A nonempty unselected queue first persists selection of its first item.
    /// An empty queue fails instead of reviving the last cleared decoder source.
    Result<void> play();
    Result<void> toggle();
    Result<void> pause();
    /// Stop decoding while retaining the selected queue item and loaded source.
    /// Later play() may restart that source; clear() also removes queue/source state.
    /// Stopping does not rewrite the durable collection membership.
    Result<void> stop();
    /// Explicit navigation is distinguished from natural end-of-track completion.
    /// Repeat-one therefore does not prevent moving to another occurrence here.
    /// The proposed selection consumes history only after queue persistence succeeds.
    Result<void> next();
    Result<void> previous();
    /// @param position Nonnegative media position in milliseconds.
    /// Negative input is rejected; upper-range handling belongs to the playback adapter.
    /// Successful dispatch does not imply that the decoder has reached the requested position.
    /// Position notifications provide the authoritative result to the frontend.
    Result<void> seek(qint64 position);
    /// @param volume Finite normalized value in [0, 1], including zero for mute.
    /// NaN and infinities are rejected before reaching the decoder.
    /// The notification contains the adapter's resulting value rather than an assumed input echo.
    Result<void> setVolume(double volume);
    /// The argument is a queue occurrence ID, not a library song ID.
    /// Selecting it reloads the source and resets navigation history.
    /// Use collection playback when starting from a library/playlist row.
    /// That path first validates and replaces the whole visible sequence.
    Result<void> playItem(int id);
    Result<void> removeItem(int id);
    Result<void> clear();
    /// @param queue Complete candidate sequence and selected index.
    /// @param play Start the selected source after the queue commits; otherwise only load it.
    /// The candidate is persisted before stopping the old source or resetting navigation.
    /// Use applyCommittedQueue when another application transaction already saved the candidate.
    Result<void> replaceQueue(PlayerQueue queue, bool play = true);
    /// Adopts a queue already saved by a collection transaction; never persists it again.
    /// currentChanged controls source reload, and resume preserves the previous play state.
    void applyCommittedQueue(PlayerQueue queue, bool currentChanged, bool resume, bool resetOrder = false);
    void metadataUpdated(const SongMetadata &metadata);
    /// Loading counts as intended playback for toggle and deletion-continuation decisions.
    /// This predicate describes user intent while decoding prepares the source.
    /// It must not be substituted for the adapter's actual decoded-audio state.
    bool playing() const { return m_state == PlayerState::Playing || m_state == PlayerState::Loading; }
    void shutdown();
    void cancelPendingMetadataRefresh() { m_metadataTimer.stop(); }
  signals:
    void stateChanged(nekotune::PlayerState state);
    void positionChanged(qint64 position, qint64 duration);
    void durationChanged(qint64 duration);
    void volumeChanged(double volume);
    void trackChanged();
    void playbackModeChanged(nekotune::PlaybackMode mode);
    void lyricsNeeded(bool metadataReady);
    void errorOccurred(const QString &message);

  private:
    Result<void> advance(PlaybackAdvance reason);
    void loadCurrent(bool play);
    void clearSource();
    void setState(PlayerState state);
    IPlaybackBackend &m_backend;
    QueueService &m_queue;
    PlaybackOrderService m_order;
    bool m_navigating = false;
    QTimer m_metadataTimer;
    AudioMetadata m_metadata;
    PlayerState m_state = PlayerState::Stopped;
    bool m_metadataReady = false;
};
} // namespace nekotune
