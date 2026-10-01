#pragma once
#include "application/queue_service.h"
#include "domain/playback_types.h"
#include <QTimer>
namespace nekotune {
class PlayerEngine final : public QObject {
    Q_OBJECT
  public:
    PlayerEngine(IPlaybackBackend &backend, QueueService &queue);
    PlaybackSnapshot snapshot() const;
    Result<void> play();
    Result<void> toggle();
    Result<void> pause();
    Result<void> stop();
    Result<void> next();
    Result<void> previous();
    Result<void> seek(qint64 position);
    Result<void> setVolume(double volume);
    Result<void> playItem(int id);
    Result<void> removeItem(int id);
    Result<void> clear();
    Result<void> replaceQueue(PlayerQueue queue, bool play = true);
    void applyCommittedQueue(PlayerQueue queue, bool currentChanged, bool resume);
    void metadataUpdated(const SongMetadata &metadata);
    bool playing() const { return m_state == PlayerState::Playing || m_state == PlayerState::Loading; }
    void shutdown();
    void cancelPendingMetadataRefresh() { m_metadataTimer.stop(); }
  signals:
    void stateChanged(nekotune::PlayerState state);
    void positionChanged(qint64 position, qint64 duration);
    void durationChanged(qint64 duration);
    void volumeChanged(double volume);
    void trackChanged();
    void lyricsNeeded(bool metadataReady);
    void errorOccurred(const QString &message);

  private:
    void loadCurrent(bool play);
    void clearSource();
    void setState(PlayerState state);
    IPlaybackBackend &m_backend;
    QueueService &m_queue;
    QTimer m_metadataTimer;
    AudioMetadata m_metadata;
    PlayerState m_state = PlayerState::Stopped;
    bool m_metadataReady = false;
};
} // namespace nekotune
