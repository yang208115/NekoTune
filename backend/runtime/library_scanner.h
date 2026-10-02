#pragma once
#include "application/command_scheduler.h"
#include "application/library/library_service.h"
#include "infrastructure/library/import_executor.h"
#include "infrastructure/library/music_directory.h"
#include <QObject>
#include <QSet>

namespace nekotune {
class LibraryScanner final : public QObject {
    Q_OBJECT
  public:
    LibraryScanner(ImportExecutor &imports, MusicDirectory &music, LibraryService &library,
                   CommandScheduler &commands);
    bool start();
    void shutdown() { m_stopping = true; }
  signals:
    void finished(int imported, int skipped, const QStringList &errors);

  private:
    void next();
    void backfillNext();
    ImportExecutor &m_imports;
    MusicDirectory &m_music;
    LibraryService &m_library;
    CommandScheduler &m_commands;
    QStringList m_paths, m_errors;
    QVector<LibrarySong> m_missingDurations;
    QSet<QString> m_inspectedHashes;
    qsizetype m_backfillIndex = 0;
    int m_index = 0, m_imported = 0, m_skipped = 0;
    bool m_active = false, m_stopping = false;
};
} // namespace nekotune
