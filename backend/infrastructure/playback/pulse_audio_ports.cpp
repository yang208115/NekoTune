#include "infrastructure/playback/pulse_audio_ports.h"
#include <algorithm>

namespace nekotune {
namespace {
void release(pa_operation *&operation) {
    if (!operation) return;
    if (pa_operation_get_state(operation) == PA_OPERATION_RUNNING)
        pa_operation_cancel(operation);
    pa_operation_unref(operation);
    operation = nullptr;
}
QString portKind(uint32_t type) {
    switch (type) {
    case PA_DEVICE_PORT_TYPE_SPEAKER: return "speaker";
    case PA_DEVICE_PORT_TYPE_HEADPHONES:
    case PA_DEVICE_PORT_TYPE_HEADSET: return "headphones";
    case PA_DEVICE_PORT_TYPE_LINE: return "lineout";
    case PA_DEVICE_PORT_TYPE_HDMI: return "hdmi";
    case PA_DEVICE_PORT_TYPE_BLUETOOTH: return "bluetooth";
    default: return {};
    }
}
}
PulseAudioPorts::PulseAudioPorts() {
    m_pump.setInterval(20);
    connect(&m_pump, &QTimer::timeout, this, [this] {
        if (m_loop) pa_mainloop_iterate(m_loop, 0, nullptr);
    });
    m_retry.setSingleShot(true);
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &PulseAudioPorts::connectServer);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(3000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        finishSelection(failure("audio_output_port_switch_failed", ErrorCode::Unavailable));
        refresh();
    });
    connectServer();
}
PulseAudioPorts::~PulseAudioPorts() {
    m_pump.stop();
    m_retry.stop();
    m_timeout.stop();
    disconnectServer();
}
void PulseAudioPorts::disconnectServer() {
    release(m_query);
    release(m_selection);
    if (m_context) {
        pa_context_set_state_callback(m_context, nullptr, nullptr);
        pa_context_set_subscribe_callback(m_context, nullptr, nullptr);
        pa_context_disconnect(m_context);
        pa_context_unref(m_context);
        m_context = nullptr;
    }
    if (m_loop) {
        pa_mainloop_free(m_loop);
        m_loop = nullptr;
    }
}
void PulseAudioPorts::connectServer() {
    disconnectServer();
    m_loop = pa_mainloop_new();
    if (!m_loop) { m_retry.start(); return; }
    m_context = pa_context_new(pa_mainloop_get_api(m_loop), "NekoTune output ports");
    if (!m_context) { m_retry.start(); return; }
    pa_context_set_state_callback(m_context, &PulseAudioPorts::stateChanged, this);
    if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0)
        m_retry.start();
    m_pump.start();
}
void PulseAudioPorts::stateChanged(pa_context *context, void *data) {
    auto *self = static_cast<PulseAudioPorts *>(data);
    switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY: {
        pa_context_set_subscribe_callback(context, [](pa_context *, pa_subscription_event_type_t, uint32_t, void *data) {
            static_cast<PulseAudioPorts *>(data)->refresh();
        }, self);
        auto *subscription = pa_context_subscribe(context, pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER), nullptr, nullptr);
        if (subscription) pa_operation_unref(subscription);
        self->refresh();
        break;
    }
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        release(self->m_query);
        self->m_outputs.clear();
        self->finishSelection(failure("audio_output_port_unavailable", ErrorCode::Unavailable));
        emit self->changed();
        self->m_retry.start();
        break;
    default: break;
    }
}
void PulseAudioPorts::refresh() {
    if (!m_context || pa_context_get_state(m_context) != PA_CONTEXT_READY) return;
    if (m_query) { m_refreshAgain = true; return; }
    m_pendingOutputs.clear();
    m_query = pa_context_get_sink_info_list(m_context, &PulseAudioPorts::sinkInfo, this);
}
void PulseAudioPorts::sinkInfo(pa_context *, const pa_sink_info *info, int end, void *data) {
    auto *self = static_cast<PulseAudioPorts *>(data);
    if (end) { self->finishRefresh(end > 0); return; }
    if (!info || !info->name) return;
    PulseOutput output;
    output.name = QString::fromUtf8(info->name);
    if (info->active_port) output.activePortId = QString::fromUtf8(info->active_port->name);
    for (uint32_t i = 0; i < info->n_ports; ++i) {
        const auto *port = info->ports[i];
        // Built-in speakers may be reported unavailable merely because headphones
        // are inserted; they remain an explicit user-selectable route.
        output.ports.append({QString::fromUtf8(port->name), QString::fromUtf8(port->description), portKind(port->type),
                             port->available != PA_PORT_AVAILABLE_NO || port->type == PA_DEVICE_PORT_TYPE_SPEAKER});
    }
    self->m_pendingOutputs.append(output);
}
void PulseAudioPorts::finishRefresh(bool success) {
    release(m_query);
    if (success && m_outputs != m_pendingOutputs) {
        m_outputs = m_pendingOutputs;
        emit changed();
    }
    if (m_readback) {
        const auto found = std::find_if(m_outputs.begin(), m_outputs.end(), [&](const auto &sink) {
            return sink.name == m_selectedSink && sink.activePortId == m_selectedPort;
        });
        finishSelection(success && found != m_outputs.end() ? Result<void>{}
            : Result<void>{failure("audio_output_port_switch_failed", ErrorCode::Unavailable)});
    }
    if (m_refreshAgain) { m_refreshAgain = false; refresh(); }
}
void PulseAudioPorts::select(const QString &sink, const QString &port, IPlaybackBackend::OutputCompletion done) {
    if (m_done || !m_context || pa_context_get_state(m_context) != PA_CONTEXT_READY) {
        done(failure("audio_output_port_unavailable", ErrorCode::Unavailable));
        return;
    }
    m_done = std::move(done);
    m_selectedSink = sink;
    m_selectedPort = port;
    m_selection = pa_context_set_sink_port_by_name(m_context, sink.toUtf8().constData(), port.toUtf8().constData(), &PulseAudioPorts::portSelected, this);
    if (!m_selection) {
        finishSelection(failure("audio_output_port_switch_failed", ErrorCode::Unavailable));
        return;
    }
    m_timeout.start();
}
void PulseAudioPorts::portSelected(pa_context *, int success, void *data) {
    auto *self = static_cast<PulseAudioPorts *>(data);
    release(self->m_selection);
    if (!success) {
        self->finishSelection(failure("audio_output_port_switch_failed", ErrorCode::Unavailable));
        return;
    }
    // Cancel an older topology query so the readback necessarily follows the write.
    release(self->m_query);
    self->m_readback = true;
    self->refresh();
}
void PulseAudioPorts::finishSelection(Result<void> result) {
    release(m_selection);
    m_timeout.stop();
    m_readback = false;
    auto done = std::move(m_done);
    m_done = {};
    if (done) done(std::move(result));
}
} // namespace nekotune
