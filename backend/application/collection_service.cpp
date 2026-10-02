#include "application/collection_service.h"
#include "application/transaction.h"
#include <QFileInfo>
#include <QSet>
#include <algorithm>
namespace nekotune {
CollectionService::CollectionService(ISongRepository &songs, IQueueRepository &queueRepo,
                                     IPlaylistRepository &playlists, ITransaction &transaction,
                                     LibraryService &library, QueueService &queue, PlayerEngine &player)
    : m_songs(songs), m_queueRepo(queueRepo), m_playlists(playlists), m_transaction(transaction),
      m_library(library), m_queue(queue), m_player(player) {}
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
    if (play)
        m_player.applyCommittedQueue(next, true, true);
    else
        m_queue.adoptCommitted(next);
    emit libraryChanged();
    return id;
}
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
Result<void> CollectionService::addSongToPlaylist(int id, int songId) {
    auto song = m_library.metadata(songId);
    if (!song)
        return song.error();
    auto path = m_library.availablePath(songId);
    if (path.isEmpty())
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
        if (!song || !QFileInfo(record.path).isFile())
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
Result<int> CollectionService::deleteSongs(const QVector<int> &ids) {
    QSet<int> selected;
    for (int id : ids) {
        if (id <= 0 || selected.contains(id) || !m_songs.songById(id))
            return failure(QStringLiteral("Invalid or missing song id"));
        selected.insert(id);
    }
    if (ids.isEmpty())
        return failure(QStringLiteral("No songs selected"));
    const auto &old = m_queue.queue();
    const int current = old.currentIndex();
    bool removed = current >= 0 && selected.contains(old.at(current).metadata.id);
    bool resume = m_player.playing();
    int nextId = 0;
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
    for (int id : ids)
        if (!m_playlists.removeSongEverywhere(id) || !m_songs.erase(id))
            return failure(m_transaction.errorString(), ErrorCode::Storage);
    if (!tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    m_player.applyCommittedQueue(next, removed, resume);
    emit libraryChanged();
    emit playlistsChanged();
    return ids.size();
}
} // namespace nekotune
