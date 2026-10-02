#pragma once
#include "application/library/library_service.h"
#include "application/library/playlist_service.h"
#include "application/playback/player_engine.h"
#include "domain/library/managed_files.h"
#include <optional>
namespace nekotune {
struct CollectionPlayResult {
    QVector<int> skippedSongIds;
};
struct CollectionDeleteResult {
    int deletedCount = 0;
    // Deletion has committed; these failures describe remaining staged files, not a rollback.
    QStringList cleanupErrors;
};
/// Coordinates operations spanning library, playlists, queue and optional managed-file cleanup.
class CollectionService final : public QObject {
    Q_OBJECT
  public:
    CollectionService(ISongRepository &songs, IQueueRepository &queueRepo, IPlaylistRepository &playlists,
                      ITransaction &transaction, LibraryService &library, QueueService &queue,
                      PlayerEngine &player, IManagedFiles *managedFiles = nullptr);
    /// @param file Inspected audio to register and append as a new queue occurrence.
    /// @param play Select and play that occurrence after both song and queue writes commit.
    /// @return Runtime queue occurrence ID; it is not the persistent song ID.
    /// With play=false, the existing selection and decoder source remain in use.
    Result<int> enqueue(const ImportedFile &file, bool play);
    /// Register inspected audio and add membership under one transaction.
    /// @param id Existing destination playlist; a missing playlist rejects the import.
    /// @param file Value snapshot from the file inspector.
    /// Success changes library/playlist state without creating a playback queue occurrence.
    Result<void> addFileToPlaylist(int id, const ImportedFile &file);
    /// @param id Destination playlist ID.
    /// @param songId Persistent library identity to resolve to a usable path.
    /// Unavailable audio is rejected because membership needs a playback source.
    /// The song's existing occurrences in the queue are unrelated to this operation.
    Result<void> addSongToPlaylist(int id, int songId);
    /// @param id Destination playlist ID.
    /// @param queueId Runtime occurrence whose stored path is copied into membership.
    /// This preserves the occurrence's source choice rather than resolving another library path.
    /// The queue itself is unchanged after a successful playlist write.
    Result<void> addQueueItemToPlaylist(int id, int queueId);
    /// songIds, when supplied, must be unique playlist members and define the playback order.
    /// @param id Playlist whose membership constrains the submitted playback context.
    /// @param songId Song to start at, or zero for the first submitted item.
    /// @param songIds Optional visible order; an explicitly empty list is invalid.
    /// All selected files must be usable before replacing the current queue.
    /// Failure retains the previous queue, source and playback-order state.
    Result<void> playPlaylist(int id, int songId = 0,
                              const std::optional<QVector<int>> &songIds = std::nullopt);
    /// @param tagIds Required tag intersection; unknown tag IDs are rejected.
    /// @param songId Song to start at, or zero for the first playable match.
    /// @param songIds Optional explicit visible order, constrained by the selected tags.
    /// @return Unavailable matched song IDs skipped from the committed playback sequence.
    /// A requested starting song cannot be silently skipped; that condition rejects playback.
    /// No playable match leaves the previous queue intact.
    Result<CollectionPlayResult> playLibrary(const QVector<int> &tagIds, int songId = 0,
                                             const std::optional<QVector<int>> &songIds = std::nullopt);
    /// Invalid IDs reject the whole batch; cleanFiles only targets registered managed assets.
    /// @param ids Nonempty unique existing song IDs to remove from every collection.
    /// @param cleanFiles Also stage registered managed assets for post-commit cleanup.
    /// @return Committed deletion count and any filesystem cleanup warnings.
    /// Database errors restore staged files; unlink errors after commit are partial success.
    /// Deleting the current song continues at its next surviving occurrence without wrapping.
    Result<CollectionDeleteResult> deleteSongs(const QVector<int> &ids, bool cleanFiles = false);
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
    IManagedFiles *m_managedFiles;
};
} // namespace nekotune
