#pragma once
#include "domain/playback/playback_backend.h"
#include "domain/result.h"
#include <functional>

namespace nekotune {
struct AudioOutputSelection {
    QString id;
    QString name;
    QString portId;
    QString portName;
};
struct AudioOutputSnapshot {
    QList<AudioOutputDevice> devices;
    AudioOutputSelection selected;
    QString activeId;
    bool available = false;
};

class AudioOutputService final : public QObject {
    Q_OBJECT
  public:
    using Saver = std::function<bool(const AudioOutputSelection &)>;
    AudioOutputService(IPlaybackBackend &backend, AudioOutputSelection selected = {}, Saver saver = {});
    AudioOutputSnapshot snapshot() const;
    Result<void> select(const QString &deviceId);
    void selectPort(const QString &deviceId, const QString &portId, IPlaybackBackend::OutputCompletion done);
  signals:
    void changed();
  private:
    void reconcile();
    IPlaybackBackend &m_backend;
    AudioOutputSelection m_selected;
    Saver m_save;
    QString m_previousActiveId;
    QString m_previousActivePort;
    bool m_switching = false;
    bool m_restorePortPending = false;
};
} // namespace nekotune
