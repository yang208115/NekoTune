#pragma once
#include "domain/playback/playback_backend.h"
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QMediaDevices>
#ifdef NEKOTUNE_PULSE_AUDIO_PORTS
#include "infrastructure/playback/pulse_audio_ports.h"
#endif
namespace nekotune {
class QtPlaybackBackend final : public IPlaybackBackend {
    Q_OBJECT
  public:
    QtPlaybackBackend();
    void setSource(const QUrl &url) override { m_player.setSource(url); }
    QUrl source() const override { return m_player.source(); }
    void play() override;
    void pause() override { m_player.pause(); }
    void stop() override { m_player.stop(); }
    void seek(qint64 value) override { m_player.setPosition(value); }
    void setVolume(double value) override { m_audio.setVolume(float(value)); }
    qint64 position() const override { return m_player.position(); }
    qint64 duration() const override { return m_player.duration(); }
    double volume() const override { return m_audio.volume(); }
    AudioMetadata metadata() const override;
    QList<AudioOutputDevice> audioOutputs() const override;
    QString activeAudioOutputId() const override { return m_outputId; }
    bool audioOutputAvailable() const override;
    void setAudioOutput(const QString &deviceId, const QString &portId = {}) override;
    void setAudioOutputPort(const QString &deviceId, const QString &portId, OutputCompletion done) override;
    void suspendAudioOutput() override;

  private:
    QAudioOutput m_audio;
    QMediaPlayer m_player;
    QMediaDevices m_devices;
    QString m_outputId;
    QString m_outputPortId, m_lastPhysicalPort;
    bool m_switchingPort = false;
#ifdef NEKOTUNE_PULSE_AUDIO_PORTS
    PulseAudioPorts m_ports;
#endif
};
} // namespace nekotune
