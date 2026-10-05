#pragma once
#include "domain/playback/playback_backend.h"
#include <algorithm>

// Deterministic device topology shared by decoder fakes; no host audio is touched.
class TestAudioBackend : public nekotune::IPlaybackBackend {
  public:
    QList<nekotune::AudioOutputDevice> devices{{"c3BlYWtlcg==", "Speaker", true}};
    QString outputId = "c3BlYWtlcg==";
    QList<nekotune::AudioOutputDevice> audioOutputs() const override { return devices; }
    QString activeAudioOutputId() const override { return outputId; }
    bool audioOutputAvailable() const override { return !outputId.isEmpty(); }
    void setAudioOutput(const QString &id, const QString &portId = {}) override {
        const auto found = std::find_if(devices.begin(), devices.end(),
                                       [&](const auto &device) { return device.id == id; });
        if (id.isEmpty() || found == devices.end()) {
            suspendAudioOutput();
            return;
        }
        if (!portId.isEmpty() && found->activePortId != portId) {
            suspendAudioOutput();
            return;
        }
        const bool wasAvailable = audioOutputAvailable();
        outputId = id;
        if (!wasAvailable)
            emit audioOutputAvailabilityChanged(true);
    }
    void suspendAudioOutput() override {
        if (outputId.isEmpty())
            return;
        outputId.clear();
        emit audioOutputAvailabilityChanged(false);
        pause();
    }
    void setDevices(QList<nekotune::AudioOutputDevice> next) {
        devices = std::move(next);
        if (std::none_of(devices.begin(), devices.end(),
                         [&](const auto &device) { return device.id == outputId; }))
            suspendAudioOutput();
        emit audioOutputsChanged();
    }
};
