#pragma once
#include "domain/file_inspector.h"
#include <QHash>
#include <QObject>
#include <QThread>
#include <atomic>
#include <functional>
#include <memory>
namespace nekotune {
class ImportExecutor final : public IFileInspector {
    Q_OBJECT
  public:
    using Completion = std::function<void(Result<ImportedFile>)>;
    ImportExecutor();
    ~ImportExecutor() override;
    void inspect(const QString &path, Completion completion) override;
    void shutdown() override;

  private:
    void complete(quint64 id, Result<ImportedFile> result);
    QThread m_thread;
    QObject *m_worker;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QHash<quint64, Completion> m_pending;
    quint64 m_nextId = 1;
};
} // namespace nekotune
