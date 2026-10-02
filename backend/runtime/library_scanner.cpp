#include "runtime/library_scanner.h"
#include <QDir>
#include <QPointer>
#include <QTimer>

namespace nekotune {
LibraryScanner::LibraryScanner(ImportExecutor &imports, MusicDirectory &music, LibraryService &library,
                               CommandScheduler &commands)
    : m_imports(imports), m_music(music), m_library(library), m_commands(commands) {}
bool LibraryScanner::start() {
    // Startup and client reconnect can both request a scan; share the active run instead of duplicating it.
    if (m_active || m_stopping)
        return false;
    m_active = true;
    m_index = m_imported = m_skipped = 0;
    m_errors.clear();
    m_inspectedHashes.clear();
    m_missingDurations.clear();
    m_backfillIndex = 0;
    // Capture unknown-duration records before discovery mutates the library.
    // Later backfill rechecks each live song because imports or deletion may supersede this snapshot.
    // Unavailable paths are excluded here rather than treated as probe failures for every restart.
    for (const auto &song : m_library.snapshot().songs)
        if (song.metadata.durationMs <= 0 && !song.path.isEmpty())
            m_missingDurations.append(song);
    if (!QDir().mkpath(m_music.directory())) {
        m_active = false;
        QTimer::singleShot(0, this, [this] { emit finished(0, 0, {"Cannot create music directory"}); });
        return true;
    }
    QPointer<LibraryScanner> guard(this);
    m_imports.discover(m_music.directory(), [guard](QStringList paths) {
        if (!guard || guard->m_stopping)
            return;
        guard->m_paths = std::move(paths);
        guard->next();
    });
    return true;
}
// Each completion schedules continuation through the event loop.
// This prevents synchronous failure chains from recursing deeply.
// The guarded pointer tolerates shutdown while inspection is running.
// Database work joins CommandScheduler rather than blocking discovery.
// A bad file is recorded and scanning proceeds to the next candidate.
void LibraryScanner::next() {
    if (m_stopping)
        return;
    if (m_index >= m_paths.size()) {
        backfillNext();
        return;
    }
    const auto path = m_paths.at(m_index++);
    QPointer<LibraryScanner> guard(this);
    m_imports.inspectUnmanaged(path, [guard, path](Result<ImportedFile> file) {
        if (!guard || guard->m_stopping)
            return;
        if (!file) {
            guard->m_errors.append(path + ": " + file.error().message);
            QTimer::singleShot(0, guard, [guard] {
                if (guard)
                    guard->next();
            });
            return;
        }
        // Record the actual inspected content hash, not the path or filename.
        // Legacy backfill can then skip bytes already probed during managed discovery.
        // Two aliases of one audio file should not cause redundant metadata recovery work.
        guard->m_inspectedHashes.insert(file.value().hash);
        // Hash/probe work is off-thread, but ignore checks and imports join the mutation queue.
        guard->m_commands.submit([guard, path, file = file.value()](auto done) {
            if (guard && !guard->m_stopping) {
                auto ignored = guard->m_music.ignored(file.hash);
                if (!ignored)
                    guard->m_errors.append(path + ": " + ignored.error().message);
                else if (ignored.value())
                    // A still-present audio file must not resurrect an explicitly deleted library song.
                    ++guard->m_skipped;
                else {
                    const bool known = guard->m_music.containsSong(file.hash);
                    auto managed = guard->m_music.manage(file);
                    if (!managed)
                        guard->m_errors.append(path + ": " + managed.error().message);
                    else {
                        auto imported = guard->m_library.importFile(managed.value());
                        if (!imported)
                            guard->m_errors.append(path + ": " + imported.error().message);
                        else if (known)
                            ++guard->m_skipped;
                        else
                            ++guard->m_imported;
                    }
                }
                QTimer::singleShot(0, guard, [guard] {
                    if (guard)
                        guard->next();
                });
            }
            done();
        });
    });
}
void LibraryScanner::backfillNext() {
    if (m_stopping)
        return;
    while (m_backfillIndex < m_missingDurations.size()) {
        // Legacy songs can live outside the managed tree; probe only those not inspected this run.
        const auto song = m_missingDurations.at(m_backfillIndex++);
        if (m_inspectedHashes.contains(song.metadata.hash))
            continue;
        // The initial missing-duration list may be stale by the time this phase starts.
        // Do not probe a deleted song or overwrite timing filled by an intervening import.
        // The write path checks hash and duration again after the worker result returns.
        const auto current = m_library.metadata(song.metadata.id);
        if (!current || current.value().durationMs > 0)
            continue;
        QPointer<LibraryScanner> guard(this);
        m_imports.inspectUnmanaged(song.path, [guard, song](Result<ImportedFile> file) {
            if (!guard || guard->m_stopping)
                return;
            guard->m_commands.submit([guard, song, file = std::move(file)](auto done) {
                if (guard && !guard->m_stopping) {
                    if (!file)
                        guard->m_errors.append(song.path + ": " + file.error().message);
                    else if (file.value().hash == song.metadata.hash) {
                        // The path may have been replaced since the snapshot; do not attach another file's duration.
                        auto result = guard->m_library.backfillDuration(song.metadata.id, file.value().hash,
                                                                       file.value().durationMs);
                        if (!result)
                            guard->m_errors.append(song.path + ": " + result.error().message);
                    }
                    QTimer::singleShot(0, guard, [guard] {
                        if (guard)
                            guard->backfillNext();
                    });
                }
                done();
            });
        });
        return;
    }
    m_active = false;
    emit finished(m_imported, m_skipped, m_errors);
}
} // namespace nekotune
