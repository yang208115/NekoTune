#include "application/download_service.h"
namespace nekotune {
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
