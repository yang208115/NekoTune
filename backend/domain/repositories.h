#pragma once
#include "domain/library/library_types.h"
#include <QHash>
namespace nekotune {
/// Transaction ownership belongs to the application use case.
/// Repositories can participate in the same unit of work without
/// knowing which other repositories the operation will modify.
/// begin() failure means the caller must not continue writing.
/// commit() failure is still a failure of the whole use case.
/// Keep the transaction alive until rollback or successful commit.
/// The SQLite implementation deliberately rejects nested begin().
/// This keeps cross-collection changes under one rollback boundary.
class ITransaction {
  public:
    virtual ~ITransaction() = default;
    virtual bool begin() = 0;
    virtual bool commit() = 0;
    virtual void rollback() = 0;
    virtual QString errorString() const = 0;
};
/// Song identity and path identity are separate concepts.
/// The hash deduplicates audio; remembered paths locate that audio.
/// Reimporting a known hash must preserve user-authored metadata.
/// Metadata updates replace supplied values, including empty text.
/// Sparse-update semantics are resolved by LibraryService first.
/// Optional lookup results distinguish missing records from values.
/// Repository write failures are reported through errorString().
/// Callers use a transaction when combining several writes.
class ISongRepository {
  public:
    virtual ~ISongRepository() = default;
    virtual QString errorString() const = 0;
    virtual std::optional<SongMetadata> getOrCreateSong(const QString &hash, const QString &path,
                                                        const QString &customTitle = {},
                                                        const QString &artist = {},
                                                        const QString &sourceName = {},
                                                        qint64 durationMs = 0) = 0;
    virtual QVector<SongMetadata> songs() const = 0;
    virtual std::optional<SongMetadata> getOrCreateRemote(const SongMetadata &) { return std::nullopt; }
    virtual std::optional<SongMetadata> songById(int songId) const = 0;
    virtual std::optional<SongMetadata> songByHash(const QString &hash) const {
        for (const auto &song : songs())
            if (song.hash == hash)
                return song;
        return std::nullopt;
    }
    virtual std::optional<SongMetadata> updateMetadata(int songId, const QString &customTitle,
                                                       const QString &artist, const QString &lyrics) = 0;
    /// Paths are candidates rather than a promise of file availability.
    /// A song may outlive a removed file or a broken managed symlink.
    /// The application chooses the first currently usable candidate.
    /// Do not delete metadata just because every path is unavailable.
    /// The bulk map supports library snapshots without per-row queries.
    virtual QVector<QString> pathsForSong(int songId) const = 0;
    virtual QHash<int, QVector<QString>> songPaths() const = 0;
    virtual bool updateDuration(int songId, qint64 durationMs) = 0;
    virtual bool erase(int songId) = 0;
};
/// Persist the playback sequence, including repeated song IDs.
/// Queue occurrence IDs are runtime identities and are not stored.
/// The current index refers to the persisted ordering of items.
/// Saving a replacement is used inside an application transaction.
/// Only after that commit may QueueService publish new memory state.
/// An empty snapshot represents an intentionally cleared queue.
class IQueueRepository {
  public:
    virtual ~IQueueRepository() = default;
    virtual QString errorString() const = 0;
    virtual bool saveQueue(const QueueSnapshot &snapshot) = 0;
    virtual QueueSnapshot loadQueue() const = 0;
};
/// Playlists are durable collections independent of the queue.
/// Each item binds a library song to the collection's chosen path.
/// Deleting a playlist does not delete the song or its audio file.
/// Removing a song everywhere is reserved for library deletion.
/// Ordering belongs to the playlist until a visible list is supplied.
/// The SQLite implementation permits one occurrence per song here,
/// whereas the playback queue permits repeated occurrences.
class IPlaylistRepository {
  public:
    virtual ~IPlaylistRepository() = default;
    virtual QString errorString() const = 0;
    virtual QVector<Playlist> playlists() const = 0;
    virtual std::optional<Playlist> playlistById(int id) const = 0;
    virtual int createPlaylist(const QString &name) = 0;
    virtual bool renamePlaylist(int id, const QString &name) = 0;
    virtual bool deletePlaylist(int id) = 0;
    virtual bool addPlaylistSong(int id, const QueueRecord &song) = 0;
    virtual bool removePlaylistSong(int id, int songId) = 0;
    virtual bool removeSongEverywhere(int songId) = 0;
};
/// Tags have stable IDs independent of their displayed names.
/// Case-insensitive normalization determines name uniqueness.
/// The display spelling remains available for UI and AI suggestions.
/// replaceSongTags receives the complete desired assignment set.
/// An empty set removes assignments without deleting reusable tags.
/// The caller surrounds replacement with the metadata transaction.
/// This prevents a partial save of title, lyrics and tag membership.
class ITagRepository {
  public:
    virtual ~ITagRepository() = default;
    virtual QString errorString() const = 0;
    virtual QVector<SongTag> tags() const = 0;
    virtual QHash<int, QVector<SongTag>> songTags() const = 0;
    virtual int createTag(const QString &name) = 0;
    virtual bool renameTag(int id, const QString &name) = 0;
    virtual bool deleteTag(int id) = 0;
    virtual bool replaceSongTags(int songId, const QStringList &names) = 0;
};
} // namespace nekotune
