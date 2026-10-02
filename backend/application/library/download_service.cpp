#include "application/library/download_service.h"
namespace nekotune {
// audioReady is a filesystem milestone, not database completion.
// Hold a scheduler slot while inspection gathers the imported value.
// Every inspection/save branch calls done() to release that slot.
// The finished event is emitted only after LibraryService commits.
// The UI can distinguish an audio download from its library result.
void DownloadService::importDownloaded(const QString &path, const QString &lyric, const QString &cover,
                                       const QString &title, const QString &artist) {
    m_commands.submit([this, path, lyric, cover, title, artist](auto done) {
        m_imports.inspect(path, [this, path, lyric, cover, title, artist, done](Result<ImportedFile> file) {
            if (!file) {
                emit importFailed(path);
                done();
                return;
            }
            auto result = m_library.importFile(file.value(), title, artist);
            if (result)
                emit finished(file.value().path, result.value().id, lyric, cover);
            else
                emit importFailed(path);
            done();
        });
    });
}
} // namespace nekotune
