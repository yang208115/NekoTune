#include "application/library/collection_service.h"
#include "application/transaction.h"
#include <QFileInfo>
#include <QSet>
#include <algorithm>
namespace nekotune {
CollectionService::CollectionService(ISongRepository &songs, IQueueRepository &queueRepo,
                                     IPlaylistRepository &playlists, ITransaction &transaction,
                                     LibraryService &library, QueueService &queue, PlayerEngine &player,
                                     IManagedFiles *managedFiles)
    : m_songs(songs), m_queueRepo(queueRepo), m_playlists(playlists), m_transaction(transaction),
      m_library(library), m_queue(queue), m_player(player), m_managedFiles(managedFiles) {}
Result<int> CollectionService::enqueue(const ImportedFile &file, bool play) {
    Transaction tx(m_transaction);
    if (!tx)
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    auto song = m_songs.getOrCreateSong(file.hash, file.path, {}, {}, file.sourceName, file.durationMs);
    if (!song)
        return failure(m_songs.errorString(), ErrorCode::Storage);
    auto next = m_queue.queue();
    auto id = next.add(file.path, *song);
    if (play)
        next.setCurrentIndex(next.size() - 1);
    next.markCurrent();
    if (!m_queueRepo.saveQueue({next.records(), next.currentIndex()}) || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    // Song registration and queue append are one transaction; publish neither on partial success.
    if (play)
        m_player.applyCommittedQueue(next, true, true, true);
    else
        m_queue.adoptCommitted(next);
    emit libraryChanged();
    return id;
}
Result<int> CollectionService::enqueueRemote(const SongMetadata &metadata, bool play) {
    Transaction tx(m_transaction);
    if (!tx)
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    auto song = m_songs.getOrCreateRemote(metadata);
    if (!song)
        return failure(m_songs.errorString(), ErrorCode::Storage);
    auto next = m_queue.queue();
    const int id = next.add(QStringLiteral(""), *song);
    if (play)
        next.setCurrentIndex(next.size() - 1);
    next.markCurrent();
    if (!m_queueRepo.saveQueue({next.records(), next.currentIndex()}) || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    if (play)
        m_player.applyCommittedQueue(next, true, true, true);
    else
        m_queue.adoptCommitted(next);
    return id;
}
Result<void> CollectionService::addRemoteToPlaylist(int id, const SongMetadata &metadata) {
    if (!m_playlists.playlistById(id))
        return failure("Playlist does not exist");
    Transaction tx(m_transaction);
    if (!tx)
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    auto song = m_songs.getOrCreateRemote(metadata);
    if (!song || !m_playlists.addPlaylistSong(id, {QStringLiteral(""), song->id}) || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    emit playlistsChanged();
    return {};
}
// Importing into a playlist spans song registration and membership.
// Validate the destination before opening the database transaction.
// Commit both writes together so a failed membership save cannot
// leave a half-completed collection operation visible to observers.
// This path neither appends to the queue nor starts playback.
Result<void> CollectionService::addFileToPlaylist(int id, const ImportedFile &file) {
    if (!m_playlists.playlistById(id))
        return failure(QStringLiteral("Playlist does not exist"));
    Transaction tx(m_transaction);
    if (!tx)
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    auto song = m_songs.getOrCreateSong(file.hash, file.path, {}, {}, file.sourceName, file.durationMs);
    if (!song || !m_playlists.addPlaylistSong(id, {file.path, song->id}) || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    emit libraryChanged();
    emit playlistsChanged();
    return {};
}
// Existing-library insertion requires a currently usable audio path.
// The playlist stores that path as its selected playback source.
// Membership is independent of any occurrences already in the queue.
// Reject missing destinations before broadcasting playlist changes.
Result<void> CollectionService::addSongToPlaylist(int id, int songId) {
    auto song = m_library.metadata(songId);
    if (!song)
        return song.error();
    auto path = m_library.availablePath(songId);
    if (path.isEmpty() && !song.value().isRemote())
        return failure(QStringLiteral("Song file is unavailable"));
    if (!m_playlists.playlistById(id))
        return failure(QStringLiteral("Playlist does not exist"));
    if (!m_playlists.addPlaylistSong(id, {path, songId}))
        return failure(m_playlists.errorString(), ErrorCode::Storage);
    emit playlistsChanged();
    return {};
}
Result<void> CollectionService::addQueueItemToPlaylist(int id, int queueId) {
    auto index = m_queue.queue().indexById(queueId);
    if (index < 0)
        return failure(QStringLiteral("Queue item does not exist"));
    const auto &item = m_queue.queue().at(index);
    if (!m_playlists.playlistById(id))
        return failure(QStringLiteral("Playlist does not exist"));
    if (!m_playlists.addPlaylistSong(id, {item.path, item.metadata.id}))
        return failure(m_playlists.errorString(), ErrorCode::Storage);
    emit playlistsChanged();
    return {};
}
Result<void> CollectionService::playPlaylist(int id, int songId,
                                            const std::optional<QVector<int>> &songIds) {
    auto playlist = m_playlists.playlistById(id);
    if (!playlist)
        return failure(QStringLiteral("Playlist does not exist"));
    if (playlist->items.isEmpty())
        return failure(QStringLiteral("Playlist is empty"));
    auto next = m_queue.queue();
    next.clear();
    int start = songId ? -1 : 0;
    auto records = playlist->items;
    if (songIds) {
        // The client supplies its visible order; membership checks prevent injecting other songs.
        records.clear();
        QSet<int> seen;
        for (int requested : *songIds) {
            auto found = std::find_if(playlist->items.cbegin(), playlist->items.cend(),
                                      [requested](const auto &record) { return record.songId == requested; });
            if (found == playlist->items.cend() || seen.contains(requested))
                return failure(QStringLiteral("Invalid song list for playlist"));
            seen.insert(requested);
            records.append(*found);
        }
        if (records.isEmpty())
            return failure(QStringLiteral("No songs selected"));
    }
    for (const auto &record : records) {
        auto song = m_songs.songById(record.songId);
        // A playlist is an explicit sequence: one unavailable entry rejects the replacement.
        if (!song || (!song->isRemote() && !QFileInfo(record.path).isFile()))
            return failure(QStringLiteral("Playlist file is unavailable: %1").arg(record.path));
        if (record.songId == songId)
            start = next.size();
        next.add(record.path, *song);
    }
    if (start < 0)
        return failure(QStringLiteral("Song is not in this playlist"));
    next.setCurrentIndex(start);
    auto result = m_player.replaceQueue(next);
    if (result)
        emit libraryChanged();
    return result;
}
Result<CollectionPlayResult> CollectionService::playLibrary(const QVector<int> &tagIds, int songId,
                                                            const std::optional<QVector<int>> &songIds) {
    auto library = m_library.snapshot();
    QSet<int> available;
    for (const auto &tag : library.tags)
        available.insert(tag.id);
    for (int id : tagIds)
        if (!available.contains(id))
            return failure(QStringLiteral("Tag does not exist"));
    auto records = library.songs;
    if (songIds) {
        records.clear();
        QSet<int> seen;
        for (int requested : *songIds) {
            auto found = std::find_if(library.songs.cbegin(), library.songs.cend(),
                                      [requested](const auto &item) { return item.metadata.id == requested; });
            if (found == library.songs.cend() || seen.contains(requested))
                return failure(QStringLiteral("Invalid song list for library"));
            seen.insert(requested);
            records.append(*found);
        }
        if (records.isEmpty())
            return failure(QStringLiteral("No songs selected"));
    }
    auto next = m_queue.queue();
    next.clear();
    CollectionPlayResult result;
    int start = songId ? -1 : 0;
    for (const auto &item : records) {
        QSet<int> assigned;
        for (const auto &tag : item.tags)
            assigned.insert(tag.id);
        bool matches = true;
        // Multiple tags are an intersection, matching the library's visible filtering rule.
        for (int id : tagIds)
            if (!assigned.contains(id)) {
                matches = false;
                break;
            }
        if (!matches && songIds)
            return failure(QStringLiteral("Selected song does not match the tags"));
        if (!matches)
            continue;
        if (item.path.isEmpty()) {
            // Library playback skips unavailable files and reports them without silently deleting them.
            result.skippedSongIds.append(item.metadata.id);
            continue;
        }
        if (item.metadata.id == songId)
            start = next.size();
        next.add(item.path, item.metadata);
    }
    if (next.isEmpty())
        return failure(QStringLiteral("No playable songs match the selected tags"));
    if (start < 0)
        return failure(QStringLiteral("Selected song is unavailable or does not match the tags"));
    next.setCurrentIndex(start);
    auto saved = m_player.replaceQueue(next);
    if (!saved)
        return saved.error();
    emit libraryChanged();
    return result;
}
// Deletion is a batch operation across all three collections.
// Validate every ID before preparing the replacement queue or files.
// Persist survivors and remove playlist references in one transaction.
// Audio hashes are also remembered to prevent automatic reimport.
// Optional managed cleanup stages renames rather than immediate unlink.
// Database failure restores those staged entries through handle lifetime.
// After commit, publish queue state and report any cleanup leftovers.
// The external original behind an audio symlink is never a target.
Result<CollectionDeleteResult> CollectionService::deleteSongs(const QVector<int> &ids, bool cleanFiles) {
    QSet<int> selected;
    QStringList hashes;
    for (int id : ids) {
        const auto song = m_songs.songById(id);
        if (id <= 0 || selected.contains(id) || !song)
            return failure(QStringLiteral("Invalid or missing song id"));
        selected.insert(id);
        if (!song->isRemote())
            hashes.append(song->hash);
    }
    if (ids.isEmpty())
        return failure(QStringLiteral("No songs selected"));
    if (cleanFiles && !m_managedFiles)
        return failure("Managed file cleanup is unavailable", ErrorCode::Unavailable);
    const auto &old = m_queue.queue();
    const int current = old.currentIndex();
    bool removed = current >= 0 && selected.contains(old.at(current).metadata.id);
    bool resume = m_player.playing();
    int nextId = 0;
    // When deleting the current track, continue at the next surviving occurrence without wrapping.
    if (removed)
        for (int i = current + 1; i < old.size(); ++i)
            if (!selected.contains(old.at(i).metadata.id)) {
                nextId = old.at(i).id;
                break;
            }
    auto next = old;
    for (int i = next.size() - 1; i >= 0; --i)
        if (selected.contains(next.at(i).metadata.id))
            next.removeAt(i);
    if (removed)
        next.setCurrentIndex(nextId ? next.indexById(nextId) : -1);
    next.markCurrent();
    Transaction tx(m_transaction);
    if (!tx || !m_queueRepo.saveQueue({next.records(), next.currentIndex()}))
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    std::unique_ptr<IManagedFileRemoval> removal;
    if (cleanFiles && !hashes.isEmpty()) {
        // Rename assets first; the handle restores them on any subsequent database failure.
        auto staged = m_managedFiles->stageRemoval(hashes);
        if (!staged)
            return staged.error();
        removal = std::move(staged.value());
    }
    for (int id : ids)
        if (!m_playlists.removeSongEverywhere(id) || !m_songs.erase(id))
            return failure(m_transaction.errorString(), ErrorCode::Storage);
    if (!tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    m_player.applyCommittedQueue(next, removed, resume);
    // Unlinking is irreversible after the database commit; report leftovers as cleanup warnings.
    const auto cleanupErrors = removal ? removal->commit() : QStringList{};
    emit libraryChanged();
    emit playlistsChanged();
    return CollectionDeleteResult{int(ids.size()), cleanupErrors};
}
} // namespace nekotune
