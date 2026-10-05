#pragma once
#include "domain/playback/playback_backend.h"
#include <QObject>
#include <QThread>
#include <functional>
#include <memory>
namespace nekotune {
class BackendSession;
/// Owns the backend thread; start/stop are called from the owner thread, never the backend thread.
class BackendRuntime final : public QObject {
    Q_OBJECT
  public:
    using PlaybackBackendFactory = std::function<std::unique_ptr<IPlaybackBackend>()>;
    explicit BackendRuntime(QObject *parent = nullptr);
    /// The factory runs on the backend thread for each new session.
    explicit BackendRuntime(PlaybackBackendFactory audioFactory, QObject *parent = nullptr);
    ~BackendRuntime() override;
    /// Construct the complete session on the backend thread before returning to the owner.
    /// Return true only after database readiness and socket listening are confirmed.
    /// Startup discovery may still be running after this synchronous admission succeeds.
    /// Failure tears down the partially constructed session and stops its thread.
    bool start();
    /// Destroy the backend session on its own thread while its event loop remains active.
    /// Only then quit and join the outer thread so service shutdown can finish callbacks.
    /// Repeated stop is harmless once that thread has terminated.
    void stop();
    QString errorString() const { return m_error; }
    QString serverName() const { return m_serverName; }

  private:
    PlaybackBackendFactory m_audioFactory;
    QThread m_thread;
    QObject *m_context = nullptr;
    BackendSession *m_session = nullptr;
    QString m_error;
    QString m_serverName;
};
} // namespace nekotune
