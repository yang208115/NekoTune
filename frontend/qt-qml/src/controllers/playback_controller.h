#pragma once
#include "controllers/feature_controller.h"
class PlaybackController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(QVariantMap song READ song NOTIFY songChanged)
  public:
    explicit PlaybackController(IpcClient &client);
    QString state() const { return m_state; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    double volume() const { return m_volume; }
    QVariantMap song() const { return m_song; }
    void apply(const QJsonObject &data);
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
    Q_INVOKABLE void toggleMute() { setVolume(m_volume > 0 ? 0 : (m_lastVolume > 0 ? m_lastVolume : .8)); }
  signals:
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void songChanged();

  private:
    QString m_state = "stopped";
    double m_position = 0, m_duration = 0, m_volume = .8, m_lastVolume = .8;
    QVariantMap m_song;
};
