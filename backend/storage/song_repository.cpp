#include "storage/song_repository.h"
#include <QSet>
#include <QSqlError>
namespace nekotune {
std::optional<SongMetadata> SongRepository::getOrCreateSong(const QString &hash, const QString &path,
                                                            const QString &customTitle,
                                                            const QString &artist) {
    if (!m_session.isReady()) {
        return std::nullopt;
    }

    if (auto existing = songByHash(hash)) {
        return rememberSongPath(existing->id, path) ? existing : std::nullopt;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO songs (hash, first_path, custom_title, artist) "
                                 "VALUES (:hash, :first_path, :custom_title, :artist)"));
    query.bindValue(QStringLiteral(":hash"), hash);
    query.bindValue(QStringLiteral(":first_path"), path);
    query.bindValue(QStringLiteral(":custom_title"),
                    customTitle.isNull() ? QStringLiteral("") : customTitle.trimmed());
    query.bindValue(QStringLiteral(":artist"), artist.isNull() ? QStringLiteral("") : artist.trimmed());

    if (!query.exec()) {
        setError(query.lastError().text());
        return std::nullopt;
    }
    const int songId = query.lastInsertId().toInt();
    if (!rememberSongPath(songId, path)) {
        return std::nullopt;
    }
    return songById(songId);
}

QVector<SongMetadata> SongRepository::songs() const {
    QVector<SongMetadata> items;
    if (!m_session.isReady()) {
        return items;
    }

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT id, hash, first_path, custom_title, artist, lyrics "
                                   "FROM songs ORDER BY id ASC"))) {
        return items;
    }

    while (query.next()) {
        items.append(SongMetadata{
            query.value(0).toInt(),
            query.value(1).toString(),
            query.value(2).toString(),
            query.value(3).toString(),
            query.value(4).toString(),
            query.value(5).toString(),
        });
    }

    return items;
}

std::optional<SongMetadata> SongRepository::songById(int songId) const {
    if (!m_session.isReady() || songId <= 0) {
        return std::nullopt;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id, hash, first_path, custom_title, artist, lyrics "
                                 "FROM songs WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), songId);

    if (!query.exec()) {
        return std::nullopt;
    }

    return readSongFromQuery(query);
}

std::optional<SongMetadata> SongRepository::updateMetadata(int songId, const QString &customTitle,
                                                           const QString &artist, const QString &lyrics) {
    if (!m_session.isReady() || songId <= 0)
        return std::nullopt;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE songs SET custom_title=:title, artist=:artist, lyrics=:lyrics, "
                                 "updated_at=CURRENT_TIMESTAMP WHERE id=:id"));
    query.bindValue(":title", customTitle.isNull() ? QStringLiteral("") : customTitle);
    query.bindValue(":artist", artist.isNull() ? QStringLiteral("") : artist);
    query.bindValue(":lyrics", lyrics.isNull() ? QStringLiteral("") : lyrics);
    query.bindValue(":id", songId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return std::nullopt;
    }
    return songById(songId);
}

QVector<QString> SongRepository::pathsForSong(int songId) const {
    QVector<QString> paths;
    if (!m_session.isReady() || songId <= 0)
        return paths;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT path FROM queue_items WHERE song_id = :id ORDER BY position"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec())
        while (query.next())
            paths.append(query.value(0).toString());
    query.prepare(
        QStringLiteral("SELECT path FROM playlist_items WHERE song_id = :id ORDER BY playlist_id, position"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec())
        while (query.next())
            paths.append(query.value(0).toString());
    query.prepare(QStringLiteral("SELECT path FROM song_paths WHERE song_id = :id ORDER BY rowid DESC"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec())
        while (query.next())
            paths.append(query.value(0).toString());
    query.prepare(QStringLiteral("SELECT first_path FROM songs WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec() && query.next())
        paths.append(query.value(0).toString());
    return paths;
}

QHash<int, QVector<QString>> SongRepository::songPaths() const {
    QHash<int, QVector<QString>> paths;
    if (!m_session.isReady())
        return paths;
    QSqlQuery query(m_db);
    if (query.exec(QStringLiteral("SELECT song_id, path FROM queue_items ORDER BY position"))) {
        while (query.next())
            paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    if (query.exec(
            QStringLiteral("SELECT song_id, path FROM playlist_items ORDER BY playlist_id, position"))) {
        while (query.next())
            paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    if (query.exec(QStringLiteral("SELECT song_id, path FROM song_paths ORDER BY rowid DESC"))) {
        while (query.next())
            paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    if (query.exec(QStringLiteral("SELECT id, first_path FROM songs ORDER BY id"))) {
        while (query.next())
            paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    return paths;
}

std::optional<SongMetadata> SongRepository::songByHash(const QString &hash) const {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id, hash, first_path, custom_title, artist, lyrics "
                                 "FROM songs WHERE hash = :hash"));
    query.bindValue(QStringLiteral(":hash"), hash);

    if (!query.exec()) {
        return std::nullopt;
    }

    return readSongFromQuery(query);
}

bool SongRepository::rememberSongPath(int songId, const QString &path) {
    if (!m_session.isReady() || songId <= 0 || path.isEmpty())
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO song_paths (song_id, path) VALUES (:id, :path)"));
    query.bindValue(QStringLiteral(":id"), songId);
    query.bindValue(QStringLiteral(":path"), path);
    if (query.exec())
        return true;
    setError(query.lastError().text());
    return false;
}

std::optional<SongMetadata> SongRepository::readSongFromQuery(QSqlQuery &query) const {
    if (!query.next()) {
        return std::nullopt;
    }

    return SongMetadata{
        query.value(0).toInt(),    query.value(1).toString(), query.value(2).toString(),
        query.value(3).toString(), query.value(4).toString(), query.value(5).toString(),
    };
}
bool SongRepository::erase(int songId) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM songs WHERE id=:id");
    query.bindValue(":id", songId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}
} // namespace nekotune
