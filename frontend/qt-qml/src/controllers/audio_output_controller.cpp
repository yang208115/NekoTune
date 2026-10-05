#include "controllers/audio_output_controller.h"
#include <QJsonArray>

AudioOutputController::AudioOutputController(IpcClient &client) : FeatureController(client) {
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        const auto name = event.value("event").toString();
        if (name == "player.audio_outputs_changed")
            apply(event);
        else if (name == "server.connected") {
            setError({});
            apply(event.value("data").toObject());
            refresh();
        }
    });
    connect(&client, &IpcClient::responseReceived, this, [this](const QString &, const QJsonObject &data) {
        if (data.contains("audio_output"))
            apply(data);
    });
}
void AudioOutputController::apply(const QJsonObject &data) {
    bool updated = false;
    if (data.contains("devices")) {
        auto devices = data.value("devices").toArray().toVariantList();
        if (devices != m_devices) {
            m_devices = std::move(devices);
            updated = true;
        }
    }
    if (data.contains("audio_output")) {
        auto output = data.value("audio_output").toObject().toVariantMap();
        if (output != m_output) {
            m_output = std::move(output);
            updated = true;
        }
    }
    if (updated)
        emit changed();
}
void AudioOutputController::setError(const QString &message) {
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}
void AudioOutputController::refresh() {
    send("player.audio_outputs", {}, [this](const auto &data, const auto &error) {
        if (error.isEmpty())
            apply(data);
        else
            setError(error);
    }, false);
}
void AudioOutputController::select(const QString &deviceId, const QString &portId) {
    if (m_busy)
        return;
    m_busy = true;
    emit busyChanged();
    setError({});
    send("player.set_audio_output", {{"device_id", deviceId}, {"port_id", portId}}, [this](const auto &data, const auto &error) {
        if (error.isEmpty())
            apply(data);
        setError(error);
        m_busy = false;
        emit busyChanged();
    }, false);
}
