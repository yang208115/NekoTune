#pragma once
#include "controllers/feature_controller.h"
/// Maintains the frontend's authoritative playback snapshot from backend events and replies.
/// Events may omit fields, so absence preserves state while an explicit zero still updates it.
/// Commands send intent; they do not optimistically rewrite the confirmed mode or song.
/// Position and duration use milliseconds, while volume uses the normalized [0, 1] range.
/// The remembered positive volume provides mute restoration without another settings store.
class PlaybackController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(QString playbackMode READ playbackMode NOTIFY playbackModeChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(QVariantMap song READ song NOTIFY songChanged)
  public:
    explicit PlaybackController(IpcClient &client);
    QString playbackMode() const { return m_playbackMode; }
    QString state() const { return m_state; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    double volume() const { return m_volume; }
    QVariantMap song() const { return m_song; }
    /// Merge a partial event or complete status reply on the frontend thread.
    /// An explicit empty song object clears selection; an omitted song retains it.
    /// Each changed field emits its own notification without resetting unrelated models.
    void apply(const QJsonObject &data);
    Q_INVOKABLE void setPlaybackMode(const QString &mode) { send("player.set_playback_mode", {{"mode", mode}}); }
    Q_INVOKABLE void playPath(const QString &path) {
        send("player.play", {{"path", IpcClient::normalizePath(path)}});
    }
    Q_INVOKABLE void play() { send("player.play"); }
    Q_INVOKABLE void togglePlayPause() { send("player.toggle_play_pause"); }
    Q_INVOKABLE void pause() { send("player.pause"); }
    Q_INVOKABLE void stop() { send("player.stop"); }
    Q_INVOKABLE void next() { send("player.next"); }
    Q_INVOKABLE void previous() { send("player.previous"); }
    Q_INVOKABLE void seek(double value) { send("player.seek", {{"position", value}}); }
    Q_INVOKABLE void setVolume(double value) { send("player.set_volume", {{"volume", value}}); }
    /// Mute is a normal zero-volume request, not a separate backend playback state.
    /// Unmuting restores the last observed positive value, with the default as a fallback.
    /// Confirmation still arrives through the ordinary volume event path.
    Q_INVOKABLE void toggleMute() { setVolume(m_volume > 0 ? 0 : (m_lastVolume > 0 ? m_lastVolume : .8)); }
  signals:
    void playbackModeChanged();
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void songChanged();

  private:
    QString m_playbackMode = "sequential";
    QString m_state = "stopped";
    double m_position = 0, m_duration = 0, m_volume = .8, m_lastVolume = .8;
    QVariantMap m_song;
};
