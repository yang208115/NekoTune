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
    void inspectUnmanaged(const QString &path, Completion completion);
    void discover(const QString &directory, std::function<void(QStringList)> completion);
    void setMapper(std::function<Result<ImportedFile>(const ImportedFile &)> mapper) {
        m_mapper = std::move(mapper);
    }
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
