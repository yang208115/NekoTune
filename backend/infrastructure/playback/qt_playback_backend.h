#pragma once
#include "domain/playback/playback_backend.h"
#include <QAudioOutput>
#include <QMediaPlayer>
namespace nekotune {
class QtPlaybackBackend final : public IPlaybackBackend {
    Q_OBJECT
  public:
    QtPlaybackBackend();
    void setSource(const QUrl &url) override { m_player.setSource(url); }
    QUrl source() const override { return m_player.source(); }
    void play() override { m_player.play(); }
    void pause() override { m_player.pause(); }
    void stop() override { m_player.stop(); }
    void seek(qint64 value) override { m_player.setPosition(value); }
    void setVolume(double value) override { m_audio.setVolume(float(value)); }
    qint64 position() const override { return m_player.position(); }
    qint64 duration() const override { return m_player.duration(); }
    double volume() const override { return m_audio.volume(); }
    AudioMetadata metadata() const override;

  private:
    QAudioOutput m_audio;
    QMediaPlayer m_player;
};
} // namespace nekotune
