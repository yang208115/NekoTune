#include "infrastructure/playback/qt_playback_backend.h"
#include <QAudioDevice>
#include <QMediaMetaData>
#include <QPointer>
namespace nekotune {
// Translate Qt Multimedia states into stable domain state values.
// Media-status loading is separate from playback-state transitions.
// Only EndOfMedia requests natural navigation through PlayerEngine.
// Decoder errors also go through the application, which owns queue repair.
// No automatic playlist/shuffle policy is implemented inside this adapter.
QtPlaybackBackend::QtPlaybackBackend() {
    m_audio.setVolume(.8);
#ifdef NEKOTUNE_PULSE_AUDIO_PORTS
    connect(&m_ports, &PulseAudioPorts::changed, this, [this] {
        if (!m_outputId.isEmpty()) {
            for (const auto &device : audioOutputs()) {
                if (device.id != m_outputId) continue;
                bool previousAvailable = m_lastPhysicalPort.isEmpty();
                for (const auto &port : device.ports)
                    if (port.id == m_lastPhysicalPort) previousAvailable = port.available;
                if (!previousAvailable || (!m_switchingPort && !audioOutputAvailable()))
                    suspendAudioOutput();
                else if (!m_switchingPort)
                    m_lastPhysicalPort = device.activePortId;
                break;
            }
        }
        emit audioOutputsChanged();
    });
#endif
    // The application restores its preference before attaching an output.
    connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, [this] {
        bool present = m_outputId.isEmpty();
        for (const auto &device : QMediaDevices::audioOutputs())
            present |= QString::fromLatin1(device.id().toBase64()) == m_outputId;
        // Port transitions can generate Qt device notifications before their readback.
        // A disappearing device still interrupts immediately during such a transition.
        if (!present || (!m_switchingPort && !m_outputId.isEmpty() && !audioOutputAvailable()))
            suspendAudioOutput();
        emit audioOutputsChanged();
    });
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, [this](auto state) {
        emit stateChanged(state == QMediaPlayer::PlayingState  ? PlayerState::Playing
                          : state == QMediaPlayer::PausedState ? PlayerState::Paused
                                                               : PlayerState::Stopped);
    });
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](auto status) {
        if (status == QMediaPlayer::EndOfMedia)
            emit ended();
        else if (status == QMediaPlayer::LoadingMedia)
            emit stateChanged(PlayerState::Loading);
        else if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)
            emit metadataChanged();
    });
    connect(&m_player, &QMediaPlayer::positionChanged, this, &IPlaybackBackend::positionChanged);
    connect(&m_player, &QMediaPlayer::durationChanged, this, &IPlaybackBackend::durationChanged);
    connect(&m_player, &QMediaPlayer::metaDataChanged, this, &IPlaybackBackend::metadataChanged);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error) {
        const auto message = m_player.errorString();
        // Losing an output is not evidence that the current audio file is corrupt.
        // Keep format/decode failures on the existing media-error path.
        if (error == QMediaPlayer::ResourceError && !audioOutputAvailable()) {
            suspendAudioOutput();
            emit audioOutputError(message);
            return;
        }
        emit failed(message);
    });
}
QList<AudioOutputDevice> QtPlaybackBackend::audioOutputs() const {
    QList<AudioOutputDevice> result;
    for (const auto &device : QMediaDevices::audioOutputs()) {
        AudioOutputDevice output{QString::fromLatin1(device.id().toBase64()), device.description(), device.isDefault()};
#ifdef NEKOTUNE_PULSE_AUDIO_PORTS
        for (const auto &sink : m_ports.outputs())
            if (sink.name.toUtf8() == device.id()) {
                output.ports = sink.ports;
                output.activePortId = sink.activePortId;
                break;
            }
#endif
        result.append(output);
    }
    return result;
}
bool QtPlaybackBackend::audioOutputAvailable() const {
    if (m_outputId.isEmpty())
        return false;
    for (const auto &device : audioOutputs())
        if (device.id == m_outputId) {
            if (m_outputPortId.isEmpty()) return true;
            for (const auto &port : device.ports)
                if (port.id == m_outputPortId)
                    return port.available && device.activePortId == m_outputPortId;
        }
    return false;
}
void QtPlaybackBackend::play() {
    if (audioOutputAvailable())
        m_player.play();
    else
        suspendAudioOutput();
}
void QtPlaybackBackend::suspendAudioOutput() {
    const bool attached = !m_outputId.isEmpty();
    m_outputId.clear();
    m_outputPortId.clear();
    m_lastPhysicalPort.clear();
    // Notify PlayerEngine first so pending source resolution loses its play intent too.
    if (attached)
        emit audioOutputAvailabilityChanged(false);
    m_player.pause();
    m_player.setAudioOutput(nullptr);
}
void QtPlaybackBackend::setAudioOutput(const QString &deviceId, const QString &portId) {
    if (!portId.isEmpty()) {
        bool usable = false;
        for (const auto &device : audioOutputs())
            if (device.id == deviceId && device.activePortId == portId)
                for (const auto &port : device.ports)
                    if (port.id == portId && port.available) usable = true;
        if (!usable) { suspendAudioOutput(); return; }
    }
    for (const auto &device : QMediaDevices::audioOutputs()) {
        if (!deviceId.isEmpty() && QString::fromLatin1(device.id().toBase64()) == deviceId) {
            const bool wasAttached = !m_outputId.isEmpty();
            m_audio.setDevice(device);
            m_outputId = deviceId;
            m_outputPortId = portId;
            for (const auto &output : audioOutputs())
                if (output.id == deviceId) m_lastPhysicalPort = output.activePortId;
            if (!wasAttached) {
                m_player.setAudioOutput(&m_audio);
                emit audioOutputAvailabilityChanged(true);
            }
            return;
        }
    }
    suspendAudioOutput();
}
void QtPlaybackBackend::setAudioOutputPort(const QString &deviceId, const QString &portId, OutputCompletion done) {
#ifdef NEKOTUNE_PULSE_AUDIO_PORTS
    for (const auto &device : audioOutputs()) {
        if (device.id != deviceId) continue;
        for (const auto &port : device.ports) {
            if (port.id != portId || !port.available) continue;
            if (device.activePortId == portId) { done({}); return; }
            if (m_switchingPort) { done(failure("audio_output_port_switch_failed", ErrorCode::Unavailable)); return; }
            m_switchingPort = true;
            QPointer<QtPlaybackBackend> guard(this);
            m_ports.select(QString::fromUtf8(QByteArray::fromBase64(deviceId.toLatin1())), portId,
                           [guard, done = std::move(done)](Result<void> result) {
                if (!guard) return;
                guard->m_switchingPort = false;
                done(std::move(result));
            });
            return;
        }
    }
#else
    Q_UNUSED(deviceId)
    Q_UNUSED(portId)
#endif
    done(failure("audio_output_port_unavailable", ErrorCode::Unavailable));
}
// Prefer the contributing performer tag for the display/search artist.
// AlbumArtist is a fallback when files omit that more specific field.
// Empty tags are legitimate and are supplemented by library metadata.
// Do not rewrite persisted metadata just because decoder tags differ.
AudioMetadata QtPlaybackBackend::metadata() const {
    auto data = m_player.metaData();
    auto artist = data.stringValue(QMediaMetaData::ContributingArtist);
    if (artist.isEmpty())
        artist = data.stringValue(QMediaMetaData::AlbumArtist);
    return {data.stringValue(QMediaMetaData::Title), artist, data.stringValue(QMediaMetaData::AlbumTitle)};
}
} // namespace nekotune
