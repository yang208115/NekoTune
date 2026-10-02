#pragma once
#include "application/command_scheduler.h"
#include "application/library/library_service.h"
#include "domain/library/file_inspector.h"
namespace nekotune {
class DownloadService final : public QObject {
    Q_OBJECT
  public:
    DownloadService(LibraryService &library, IFileInspector &imports, CommandScheduler &commands)
        : m_library(library), m_imports(imports), m_commands(commands) {}
    void importDownloaded(const QString &path, const QString &lyric, const QString &cover,
                          const QString &title, const QString &artist);

  private:
    LibraryService &m_library;
    IFileInspector &m_imports;
    CommandScheduler &m_commands;
  signals:
    void finished(const QString &path, int songId, const QString &lyricStatus, const QString &coverStatus);
    void importFailed(const QString &path);
};
} // namespace nekotune
