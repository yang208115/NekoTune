#pragma once
#include "application/command_scheduler.h"
#include "application/library/library_service.h"
#include "domain/library/file_inspector.h"
namespace nekotune {
/// Bridges saved provider audio to the local-first library import path.
/// Downloaded audio still needs inspection and its local SHA-256.
/// The mutation scheduler orders import against collection changes.
/// Optional lyric/cover statuses are forwarded as outcome metadata.
/// An import failure preserves the already saved audio for recovery.
/// Successful download/import does not mutate the playback queue.
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
