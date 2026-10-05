#include "application/playback/audio_output_service.h"
#include <algorithm>
#include <QPointer>

namespace nekotune {
AudioOutputService::AudioOutputService(IPlaybackBackend &backend, AudioOutputSelection selected, Saver saver)
    : m_backend(backend), m_selected(std::move(selected)), m_save(std::move(saver)) {
    m_restorePortPending = !m_selected.portId.isEmpty();
    connect(&backend, &IPlaybackBackend::audioOutputsChanged, this, &AudioOutputService::reconcile);
    connect(&backend, &IPlaybackBackend::audioOutputError, this, [this] { emit changed(); });
    reconcile();
}
AudioOutputSnapshot AudioOutputService::snapshot() const {
    return {m_backend.audioOutputs(), m_selected, m_backend.activeAudioOutputId(),
            m_backend.audioOutputAvailable()};
}
Result<void> AudioOutputService::select(const QString &deviceId) {
    if (m_switching)
        return failure("audio_output_switching", ErrorCode::Unavailable);
    AudioOutputSelection next{deviceId, {}};
    if (!deviceId.isEmpty()) {
        const auto devices = m_backend.audioOutputs();
        const auto found = std::find_if(devices.begin(), devices.end(),
                                       [&](const auto &device) { return device.id == deviceId; });
        if (found == devices.end())
            return failure("audio_output_device_unavailable", ErrorCode::Unavailable);
        next.name = found->name;
    }
    if (m_save && !m_save(next))
        return failure("audio_output_save_failed", ErrorCode::Storage);
    m_selected = std::move(next);
    m_restorePortPending = false;
    reconcile();
    return {};
}
void AudioOutputService::selectPort(const QString &deviceId, const QString &portId, IPlaybackBackend::OutputCompletion done) {
    if (m_switching) { done(failure("audio_output_switching", ErrorCode::Unavailable)); return; }
    const auto devices = m_backend.audioOutputs();
    AudioOutputSelection next;
    QString previousPort;
    for (const auto &device : devices)
        if (device.id == deviceId)
            for (const auto &port : device.ports)
                if (port.id == portId && port.available) {
                    next = {device.id, device.name, port.id, port.name};
                    previousPort = device.activePortId;
                }
    if (next.portId.isEmpty()) { done(failure("audio_output_port_unavailable", ErrorCode::Unavailable)); return; }
    m_switching = true;
    QPointer<AudioOutputService> guard(this);
    m_backend.setAudioOutputPort(deviceId, portId,
        [guard, next, previousPort, done = std::move(done)](Result<void> result) mutable {
            if (!guard) return;
            if (!result) {
                guard->m_switching = false;
                guard->reconcile();
                done(std::move(result));
                return;
            }
            if (guard->m_save && !guard->m_save(next)) {
                auto restored = [guard, done = std::move(done)](Result<void> rollback) mutable {
                    if (!guard) return;
                    guard->m_switching = false;
                    if (rollback) {
                        guard->reconcile();
                    } else {
                        guard->m_backend.suspendAudioOutput();
                        guard->m_previousActiveId.clear();
                        guard->m_previousActivePort.clear();
                        emit guard->changed();
                    }
                    done(failure(rollback ? "audio_output_save_failed" : "audio_output_restore_failed", ErrorCode::Storage));
                };
                if (previousPort.isEmpty() || previousPort == next.portId)
                    restored({});
                else
                    guard->m_backend.setAudioOutputPort(next.id, previousPort, std::move(restored));
                return;
            }
            guard->m_selected = next;
            guard->m_restorePortPending = false;
            guard->m_switching = false;
            guard->reconcile();
            done({});
        });
}
void AudioOutputService::reconcile() {
    if (m_switching) return;
    const auto devices = m_backend.audioOutputs();
    QString target;
    bool previousPresent = m_previousActiveId.isEmpty();
    for (const auto &device : devices) {
        if (device.id == m_previousActiveId) {
            previousPresent = m_previousActivePort.isEmpty();
            for (const auto &port : device.ports)
                if (port.id == m_previousActivePort) previousPresent = port.available;
        }
        if (m_selected.id.isEmpty() ? device.isDefault : device.id == m_selected.id) {
            target = device.id;
            if (!m_selected.id.isEmpty())
                m_selected.name = device.name;
        }
    }
    // A disappearing physical output pauses even in follow-default mode. Reattaching
    // a replacement below must never restore the old play intent.
    if (!previousPresent)
        m_backend.suspendAudioOutput();
    if (target.isEmpty() && !m_selected.portId.isEmpty())
        m_restorePortPending = true;
    if (!target.isEmpty() && !m_selected.portId.isEmpty()) {
        bool portAvailable = false, portActive = false;
        for (const auto &device : devices)
            if (device.id == target) {
                portActive = device.activePortId == m_selected.portId;
                for (const auto &port : device.ports)
                    if (port.id == m_selected.portId) portAvailable = port.available;
            }
        if (!portAvailable) m_restorePortPending = true;
        if (portAvailable && !portActive && m_restorePortPending) {
            m_restorePortPending = false;
            m_switching = true;
            m_backend.suspendAudioOutput();
            QPointer<AudioOutputService> guard(this);
            m_backend.setAudioOutputPort(target, m_selected.portId, [guard](Result<void>) {
                if (!guard) return;
                guard->m_switching = false;
                guard->reconcile();
            });
            return;
        }
        if (!portAvailable || !portActive) target.clear();
        else m_restorePortPending = false;
    }
    m_backend.setAudioOutput(target, m_selected.portId);
    m_previousActiveId = m_backend.activeAudioOutputId();
    m_previousActivePort.clear();
    for (const auto &device : devices)
        if (device.id == m_previousActiveId) m_previousActivePort = device.activePortId;
    emit changed();
}
} // namespace nekotune
