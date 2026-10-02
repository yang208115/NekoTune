#pragma once
#include "domain/library/library_types.h"
#include <QHash>
namespace nekotune {
class ITransaction {
  public:
    virtual ~ITransaction() = default;
    virtual bool begin() = 0;
    virtual bool commit() = 0;
    virtual void rollback() = 0;
    virtual QString errorString() const = 0;
};
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
    virtual std::optional<SongMetadata> songById(int songId) const = 0;
    virtual std::optional<SongMetadata> updateMetadata(int songId, const QString &customTitle,
                                                       const QString &artist, const QString &lyrics) = 0;
    virtual QVector<QString> pathsForSong(int songId) const = 0;
    virtual QHash<int, QVector<QString>> songPaths() const = 0;
    virtual bool updateDuration(int songId, qint64 durationMs) = 0;
    virtual bool erase(int songId) = 0;
};
class IQueueRepository {
  public:
    virtual ~IQueueRepository() = default;
    virtual QString errorString() const = 0;
    virtual bool saveQueue(const QueueSnapshot &snapshot) = 0;
    virtual QueueSnapshot loadQueue() const = 0;
};
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
