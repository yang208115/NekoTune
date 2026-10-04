#include "storage/playlist_repository.h"
#include <QSet>
#include <QSqlError>
namespace nekotune {
// LEFT JOIN preserves empty playlists in the returned collection list.
// Ordering by playlist then position lets one pass assemble each vector.
// NULL item identity belongs to the empty join row, not a real song.
// The service can therefore distinguish an empty existing playlist from
// a missing playlist before attempting any playback replacement.
QVector<Playlist> PlaylistRepository::playlists() const {
    QVector<Playlist> result;
    if (!m_session.isReady())
        return result;
    QSqlQuery query(m_db);
    if (!query.exec(
            QStringLiteral("SELECT p.id, p.name, i.path, i.song_id FROM playlists p "
                           "LEFT JOIN playlist_items i ON i.playlist_id = p.id ORDER BY p.id, i.position")))
        return result;
    while (query.next()) {
        const int id = query.value(0).toInt();
        if (result.isEmpty() || result.last().id != id)
            result.append({id, query.value(1).toString(), {}});
        if (!query.value(3).isNull())
            result.last().items.append({query.value(2).toString(), query.value(3).toInt()});
    }
    return result;
}

std::optional<Playlist> PlaylistRepository::playlistById(int id) const {
    for (const auto &playlist : playlists()) {
        if (playlist.id == id)
            return playlist;
    }
    return std::nullopt;
}

int PlaylistRepository::createPlaylist(const QString &name) {
    if (!m_session.isReady())
        return 0;
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 128) {
        setError(QStringLiteral("Playlist name must contain 1 to 128 characters"));
        return 0;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO playlists (name) VALUES (:name)"));
    query.bindValue(QStringLiteral(":name"), trimmed);
    if (!query.exec()) {
        setError(query.lastError().text());
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool PlaylistRepository::renamePlaylist(int id, const QString &name) {
    if (!m_session.isReady())
        return false;
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 128) {
        setError(QStringLiteral("Playlist name must contain 1 to 128 characters"));
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE playlists SET name = :name WHERE id = :id"));
    query.bindValue(QStringLiteral(":name"), trimmed);
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        setError(QStringLiteral("Playlist does not exist"));
        return false;
    }
    return true;
}

bool PlaylistRepository::deletePlaylist(int id) {
    if (!m_session.isReady())
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM playlists WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        setError(QStringLiteral("Playlist does not exist"));
        return false;
    }
    return true;
}

// Append only a newly associated song at the end of this playlist.
// An existing membership updates its path without moving its position.
// Position gaps left by removal are acceptable ordering keys.
// The next append uses the maximum rather than the item count.
// This avoids collisions after deletions in the middle of a playlist.
bool PlaylistRepository::addPlaylistSong(int id, const QueueRecord &song) {
    if (!m_session.isReady())
        return false;
    if (song.path.isEmpty()) {
        QSqlQuery source(m_db);
        source.prepare("SELECT 1 FROM songs WHERE id=:id AND source_provider<>''");
        source.bindValue(":id", song.songId);
        if (!source.exec() || !source.next()) {
            setError(QStringLiteral("Song path is required"));
            return false;
        }
    }
    QSqlQuery query(m_db);
    // A song can belong to several playlists, but occurs only once within each.
    query.prepare(QStringLiteral("INSERT INTO playlist_items (playlist_id, song_id, path, position) "
                                 "VALUES (:id, :song_id, :path, (SELECT COALESCE(MAX(position), -1) + 1 FROM "
                                 "playlist_items WHERE playlist_id = :id)) "
                                 "ON CONFLICT (playlist_id, song_id) DO UPDATE SET path = excluded.path"));
    query.bindValue(QStringLiteral(":id"), id);
    query.bindValue(QStringLiteral(":song_id"), song.songId);
    query.bindValue(QStringLiteral(":path"), song.path.isNull() ? QStringLiteral("") : song.path);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return true;
}

bool PlaylistRepository::removePlaylistSong(int id, int songId) {
    if (!m_session.isReady())
        return false;
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = :id AND song_id = :song_id"));
    query.bindValue(QStringLiteral(":id"), id);
    query.bindValue(QStringLiteral(":song_id"), songId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        setError(QStringLiteral("Song is not in this playlist"));
        return false;
    }
    return true;
}
bool PlaylistRepository::removeSongEverywhere(int songId) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM playlist_items WHERE song_id=:id");
    query.bindValue(":id", songId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return true;
}
} // namespace nekotune
