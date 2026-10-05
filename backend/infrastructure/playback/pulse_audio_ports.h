#pragma once
#include "domain/playback/playback_backend.h"
#include <QTimer>
#include <pulse/pulseaudio.h>

namespace nekotune {
struct PulseOutput {
    QString name;
    QList<AudioOutputPort> ports;
    QString activePortId;
    bool operator==(const PulseOutput &) const = default;
};

// The PulseAudio protocol also covers PipeWire's Pulse server. All callbacks run
// on the owning Qt thread; nonblocking mainloop iterations never wait for the server.
class PulseAudioPorts final : public QObject {
    Q_OBJECT
  public:
    PulseAudioPorts();
    ~PulseAudioPorts() override;
    QList<PulseOutput> outputs() const { return m_outputs; }
    void select(const QString &sink, const QString &port, IPlaybackBackend::OutputCompletion done);
  signals:
    void changed();
  private:
    void connectServer();
    void disconnectServer();
    void refresh();
    void finishRefresh(bool success);
    void finishSelection(Result<void> result);
    static void stateChanged(pa_context *, void *);
    static void sinkInfo(pa_context *, const pa_sink_info *, int, void *);
    static void portSelected(pa_context *, int, void *);
    pa_mainloop *m_loop = nullptr;
    pa_context *m_context = nullptr;
    pa_operation *m_query = nullptr;
    pa_operation *m_selection = nullptr;
    QTimer m_pump, m_retry, m_timeout;
    QList<PulseOutput> m_outputs, m_pendingOutputs;
    bool m_refreshAgain = false, m_readback = false;
    QString m_selectedSink, m_selectedPort;
    IPlaybackBackend::OutputCompletion m_done;
};
} // namespace nekotune
