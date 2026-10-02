#include "storage/song_repository.h"
#include <QSet>
#include <QSqlError>
namespace nekotune {
std::optional<SongMetadata> SongRepository::getOrCreateSong(const QString &hash, const QString &path,
                                                            const QString &customTitle, const QString &artist,
                                                            const QString &sourceName, qint64 durationMs) {
    if (!m_session.isReady()) {
        return std::nullopt;
    }

    QSqlQuery restore(m_db);
    restore.prepare("DELETE FROM scan_ignored WHERE hash=:hash");
    restore.bindValue(":hash", hash);
    if (!restore.exec()) {
        setError(restore.lastError().text());
        return std::nullopt;
    }
    if (auto existing = songByHash(hash)) {
        if (!rememberSongPath(existing->id, path))
            return std::nullopt;
        if (existing->sourceName.isEmpty() && !sourceName.isEmpty()) {
            QSqlQuery update(m_db);
            update.prepare("UPDATE songs SET source_name=:name WHERE id=:id");
            update.bindValue(":name", sourceName);
            update.bindValue(":id", existing->id);
            if (!update.exec()) {
                setError(update.lastError().text());
                return std::nullopt;
            }
            existing->sourceName = sourceName;
        }
        if (durationMs > 0 && existing->durationMs != durationMs) {
            if (!updateDuration(existing->id, durationMs))
                return std::nullopt;
            existing->durationMs = durationMs;
        }
        return existing;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO songs (hash, first_path, custom_title, artist, "
                                 "source_name, duration_ms) "
                                 "VALUES (:hash, :first_path, :custom_title, :artist, :source_name, :duration_ms)"));
    query.bindValue(":duration_ms", qMax(qint64(0), durationMs));
    query.bindValue(":source_name", sourceName.isNull() ? QStringLiteral("") : sourceName);
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
    if (!query.exec(QStringLiteral("SELECT id, hash, first_path, custom_title, artist, lyrics, "
                                 "source_name, duration_ms "
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
            query.value(6).toString(),
            query.value(7).toLongLong(),
        });
    }

    return items;
}

std::optional<SongMetadata> SongRepository::songById(int songId) const {
    if (!m_session.isReady() || songId <= 0) {
        return std::nullopt;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id, hash, first_path, custom_title, artist, lyrics, "
                                 "source_name, duration_ms "
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
    query.prepare(QStringLiteral("SELECT id, hash, first_path, custom_title, artist, lyrics, "
                                 "source_name, duration_ms "
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
        query.value(6).toString(), query.value(7).toLongLong(),
    };
}
bool SongRepository::updateDuration(int songId, qint64 durationMs) {
    if (!m_session.isReady() || songId <= 0 || durationMs <= 0)
        return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE songs SET duration_ms=:duration, updated_at=CURRENT_TIMESTAMP WHERE id=:id");
    query.bindValue(":duration", durationMs);
    query.bindValue(":id", songId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}
bool SongRepository::erase(int songId) {
    QSqlQuery query(m_db);
    query.prepare("INSERT OR IGNORE INTO scan_ignored SELECT hash FROM songs WHERE id=:id");
    query.bindValue(":id", songId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    query.prepare("DELETE FROM songs WHERE id=:id");
    query.bindValue(":id", songId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}
} // namespace nekotune
