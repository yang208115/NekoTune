#pragma once
#include "application/library/library_service.h"
#include "application/transaction.h"
#include "storage/database_session.h"
#include "storage/playlist_repository.h"
#include "storage/queue_repository.h"
#include "storage/song_repository.h"
#include "storage/tag_repository.h"
#include <QSet>
namespace nekotune {
/// Test composition root for real SQLite repositories and library services.
/// It supplies temporary paths rather than a parallel fake storage implementation.
/// Thin helpers preserve production transaction semantics for multi-write operations.
/// Declaration order keeps the shared database alive until repositories are destroyed.
/// Collection tests can inject SQL failures while inspecting persisted state directly.
/// Its deletion helper intentionally excludes filesystem/audio adapter responsibilities.
class StoreFixture final {
  public:
    StoreFixture(const QString &path = DatabaseSession::defaultDatabasePath(), const QString &name = {})
        : db(path, name), songRepo(db), queueRepo(db), playlistRepo(db), tagRepo(db),
          library(songRepo, tagRepo, db) {}
    bool isReady() const { return db.isReady(); }
    QString errorString() const { return db.errorString(); }
    QString databasePath() const { return db.databasePath(); }
    static QString defaultDatabasePath() { return DatabaseSession::defaultDatabasePath(); }
    std::optional<SongMetadata> getOrCreateSong(const QString &hash, const QString &path,
                                                const QString &title = {}, const QString &artist = {}) {
        auto result = library.importFile({path, hash}, title, artist);
        return result ? std::optional(result.value()) : std::nullopt;
    }
    QVector<SongMetadata> songs() const { return songRepo.songs(); }
    std::optional<SongMetadata> songById(int id) const { return songRepo.songById(id); }
    std::optional<SongMetadata> updateMetadata(int id, const QString &title, const QString &artist,
                                               const QString &lyrics,
                                               const std::optional<QStringList> &tags = std::nullopt) {
        auto result = library.update(id, {title, artist, lyrics, tags});
        return result ? std::optional(result.value()) : std::nullopt;
    }
    bool saveQueue(const QueueSnapshot &queue) {
        Transaction tx(db);
        return tx && queueRepo.saveQueue(queue) && tx.commit();
    }
    QueueSnapshot loadQueue() const { return queueRepo.loadQueue(); }
    QVector<QString> pathsForSong(int id) const { return songRepo.pathsForSong(id); }
    QHash<int, QVector<QString>> songPaths() const { return songRepo.songPaths(); }
    QVector<Playlist> playlists() const { return playlistRepo.playlists(); }
    std::optional<Playlist> playlistById(int id) const { return playlistRepo.playlistById(id); }
    int createPlaylist(const QString &name) { return playlistRepo.createPlaylist(name); }
    bool renamePlaylist(int id, const QString &name) { return playlistRepo.renamePlaylist(id, name); }
    bool deletePlaylist(int id) { return playlistRepo.deletePlaylist(id); }
    bool addPlaylistSong(int id, const QueueRecord &song) { return playlistRepo.addPlaylistSong(id, song); }
    bool removePlaylistSong(int id, int songId) { return playlistRepo.removePlaylistSong(id, songId); }
    QVector<SongTag> tags() const { return tagRepo.tags(); }
    QHash<int, QVector<SongTag>> songTags() const { return tagRepo.songTags(); }
    int createTag(const QString &name) { return tagRepo.createTag(name); }
    bool renameTag(int id, const QString &name) { return tagRepo.renameTag(id, name); }
    bool deleteTag(int id) { return tagRepo.deleteTag(id); }
    bool deleteSongs(const QVector<int> &ids, const QueueSnapshot &queue) {
        QSet<int> seen;
        for (int id : ids) {
            if (id <= 0 || seen.contains(id) || !songRepo.songById(id))
                return false;
            seen.insert(id);
        }
        if (ids.isEmpty())
            return false;
        if (queue.currentIndex < -1 || queue.currentIndex >= queue.items.size())
            return false;
        for (const auto &record : queue.items)
            if (seen.contains(record.songId))
                return false;
        Transaction tx(db);
        if (!tx || !queueRepo.saveQueue(queue))
            return false;
        for (int id : ids)
            if (!playlistRepo.removeSongEverywhere(id) || !songRepo.erase(id))
                return false;
        return tx.commit();
    }
    DatabaseSession db;
    SongRepository songRepo;
    QueueRepository queueRepo;
    PlaylistRepository playlistRepo;
    TagRepository tagRepo;
    LibraryService library;
};
} // namespace nekotune
