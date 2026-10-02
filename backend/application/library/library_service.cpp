#include "application/library/library_service.h"
#include "application/transaction.h"
#include <QFileInfo>
namespace nekotune {
LibraryService::LibraryService(ISongRepository &songs, ITagRepository &tags, ITransaction &transaction)
    : m_songs(songs), m_tags(tags), m_transaction(transaction) {}
LibrarySnapshot LibraryService::snapshot() const {
    LibrarySnapshot result;
    result.tags = m_tags.tags();
    const auto paths = m_songs.songPaths();
    const auto memberships = m_tags.songTags();
    for (const auto &song : m_songs.songs()) {
        QString path;
        for (const auto &candidate : paths.value(song.id))
            if (QFileInfo(candidate).isFile()) {
                path = candidate;
                break;
            }
        result.songs.append({song, path, memberships.value(song.id)});
    }
    return result;
}
Result<SongMetadata> LibraryService::metadata(int id) const {
    auto song = m_songs.songById(id);
    if (!song)
        return failure(QStringLiteral("Song metadata not found"), ErrorCode::NotFound);
    return *song;
}
QVector<SongTag> LibraryService::tagsFor(int id) const { return m_tags.songTags().value(id); }
QString LibraryService::availablePath(int id) const {
    for (const auto &path : m_songs.pathsForSong(id))
        if (QFileInfo(path).isFile())
            return path;
    return {};
}
Result<SongMetadata> LibraryService::importFile(const ImportedFile &file, const QString &title,
                                                const QString &artist) {
    Transaction tx(m_transaction);
    if (!tx)
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    auto song = m_songs.getOrCreateSong(file.hash, file.path, title, artist, file.sourceName, file.durationMs);
    if (!song || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    emit durationUpdated(*song);
    emit changed();
    return *song;
}
Result<void> LibraryService::backfillDuration(int id, const QString &hash, qint64 durationMs) {
    // A scan may finish after deletion or another import; never recreate or replace a song here.
    auto song = m_songs.songById(id);
    if (!song || song->hash != hash || song->durationMs > 0 || durationMs <= 0)
        return {};
    Transaction tx(m_transaction);
    if (!tx || !m_songs.updateDuration(id, durationMs) || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    song->durationMs = durationMs;
    emit durationUpdated(*song);
    emit changed();
    return {};
}
Result<SongMetadata> LibraryService::update(int id, const MetadataPatch &patch) {
    auto current = metadata(id);
    if (!current)
        return current.error();
    Transaction tx(m_transaction);
    if (!tx)
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    const auto &song = current.value();
    auto updated =
        m_songs.updateMetadata(id, patch.title.value_or(song.customTitle), patch.artist.value_or(song.artist),
                               patch.lyrics.value_or(song.lyrics));
    if (!updated || (patch.tags && !m_tags.replaceSongTags(id, *patch.tags)) || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    emit metadataChanged(*updated);
    emit changed();
    return *updated;
}
} // namespace nekotune
