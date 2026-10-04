#pragma once
#include "domain/library/file_inspector.h"
#include <QHash>
#include <QObject>
#include <QThread>
#include <atomic>
#include <functional>
#include <memory>
namespace nekotune {
/// Hashes/probes files on a worker, then delivers inspection callbacks on the owning backend thread.
class ImportExecutor final : public IFileInspector {
    Q_OBJECT
  public:
    using Completion = std::function<void(Result<ImportedFile>)>;
    ImportExecutor();
    ~ImportExecutor() override;
    /// @param path Local source whose bytes are hashed and timing metadata probed off-thread.
    /// @param completion Owner-thread callback for inspection followed by optional mapping.
    /// The mapper may register managed paths because it runs after returning to the SQL owner.
    /// An unknown duration does not turn an otherwise valid audio inspection into failure.
    void inspect(const QString &path, Completion completion) override;
    /// Inspect a remembered source without invoking the configured managed-path mapper.
    /// Use this when recovering legacy duration rather than registering a new managed resource.
    /// Completion still returns to the executor's owning thread.
    void inspectUnmanaged(const QString &path, Completion completion);
    /// Discover supported local audio and numbered references without traversing linked directories.
    /// The callback receives paths to inspect, not registered library records.
    /// Discovery is off-thread and does not choose a playback context.
    void discover(const QString &directory, std::function<void(QStringList)> completion);
    /// Runs after inspection on the owner thread, so mapping may safely use backend-owned SQL state.
    void setMapper(std::function<Result<ImportedFile>(const ImportedFile &)> mapper) {
        m_mapper = std::move(mapper);
    }
    /// Set shared cancellation before joining the file-inspection worker.
    /// Accepted inspection callbacks are completed with cancellation rather than silently dropped.
    /// No worker-owned database connection is needed because mapping remains on the owner thread.
    void shutdown() override;

  private:
    void complete(quint64 id, Result<ImportedFile> result);
    QThread m_thread;
    QObject *m_worker;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QHash<quint64, Completion> m_pending;
    quint64 m_nextId = 1;
    std::function<Result<ImportedFile>(const ImportedFile &)> m_mapper;
};
} // namespace nekotune
