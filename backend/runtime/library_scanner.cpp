#include "runtime/library_scanner.h"
#include <QDir>
#include <QPointer>
#include <QTimer>

namespace nekotune {
LibraryScanner::LibraryScanner(ImportExecutor &imports, MusicDirectory &music, LibraryService &library,
                               CommandScheduler &commands)
    : m_imports(imports), m_music(music), m_library(library), m_commands(commands) {}
bool LibraryScanner::start() {
    if (m_active || m_stopping)
        return false;
    m_active = true;
    m_index = m_imported = m_skipped = 0;
    m_errors.clear();
    m_inspectedHashes.clear();
    m_missingDurations.clear();
    m_backfillIndex = 0;
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
        guard->m_inspectedHashes.insert(file.value().hash);
        guard->m_commands.submit([guard, path, file = file.value()](auto done) {
            if (guard && !guard->m_stopping) {
                auto ignored = guard->m_music.ignored(file.hash);
                if (!ignored)
                    guard->m_errors.append(path + ": " + ignored.error().message);
                else if (ignored.value())
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
        const auto song = m_missingDurations.at(m_backfillIndex++);
        if (m_inspectedHashes.contains(song.metadata.hash))
            continue;
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
