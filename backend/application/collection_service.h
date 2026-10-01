#pragma once
#include "application/library_service.h"
#include "application/playlist_service.h"
#include "core/player_engine.h"
#include <optional>
namespace nekotune {
struct CollectionPlayResult {
    QVector<int> skippedSongIds;
};
class CollectionService final : public QObject {
    Q_OBJECT
  public:
    CollectionService(ISongRepository &songs, IQueueRepository &queueRepo, IPlaylistRepository &playlists,
                      ITransaction &transaction, LibraryService &library, QueueService &queue,
                      PlayerEngine &player);
    Result<int> enqueue(const ImportedFile &file, bool play);
    Result<void> addFileToPlaylist(int id, const ImportedFile &file);
    Result<void> addSongToPlaylist(int id, int songId);
    Result<void> addQueueItemToPlaylist(int id, int queueId);
    Result<void> playPlaylist(int id, int songId = 0,
                              const std::optional<QVector<int>> &songIds = std::nullopt);
    Result<CollectionPlayResult> playLibrary(const QVector<int> &tagIds, int songId = 0,
                                             const std::optional<QVector<int>> &songIds = std::nullopt);
    Result<int> deleteSongs(const QVector<int> &ids);
  signals:
    void libraryChanged();
    void playlistsChanged();

  private:
    ISongRepository &m_songs;
    IQueueRepository &m_queueRepo;
    IPlaylistRepository &m_playlists;
    ITransaction &m_transaction;
    LibraryService &m_library;
    QueueService &m_queue;
    PlayerEngine &m_player;
};
} // namespace nekotune
