#include "storage/song_store.h"

#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

namespace nekotune {

namespace {

QString normalizedName(const QString &name)
{
    return name.trimmed().toCaseFolded();
}

bool validName(const QString &name)
{
    const auto trimmed = name.trimmed();
    return !trimmed.isEmpty() && trimmed.size() <= 64;
}

} // namespace

QVector<SongTag> SongStore::tags() const
{
    QVector<SongTag> result;
    if (!m_ready) return result;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT id, name FROM tags ORDER BY normalized_name, id"))) return result;
    while (query.next()) result.append({query.value(0).toInt(), query.value(1).toString()});
    return result;
}

QHash<int, QVector<SongTag>> SongStore::songTags() const
{
    QHash<int, QVector<SongTag>> result;
    if (!m_ready) return result;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT st.song_id, t.id, t.name FROM song_tags st "
            "JOIN tags t ON t.id = st.tag_id ORDER BY st.song_id, t.normalized_name, t.id")))
        return result;
    while (query.next())
        result[query.value(0).toInt()].append({query.value(1).toInt(), query.value(2).toString()});
    return result;
}

int SongStore::createTag(const QString &name)
{
    if (!m_ready) return 0;
    if (!validName(name)) {
        setError(QStringLiteral("Tag name must contain 1 to 64 characters"));
        return 0;
    }
    QSqlQuery existing(m_db);
    existing.prepare(QStringLiteral("SELECT id FROM tags WHERE normalized_name = :key"));
    existing.bindValue(QStringLiteral(":key"), normalizedName(name));
    if (!existing.exec()) { setError(existing.lastError().text()); return 0; }
    if (existing.next()) { setError(QStringLiteral("Tag already exists")); return 0; }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO tags (name, normalized_name) VALUES (:name, :normalized_name)"));
    query.bindValue(QStringLiteral(":name"), name.trimmed());
    query.bindValue(QStringLiteral(":normalized_name"), normalizedName(name));
    if (!query.exec()) {
        setError(query.lastError().text());
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool SongStore::renameTag(int id, const QString &name)
{
    if (!m_ready || id <= 0) return false;
    if (!validName(name)) {
        setError(QStringLiteral("Tag name must contain 1 to 64 characters"));
        return false;
    }
    QSqlQuery existing(m_db);
    existing.prepare(QStringLiteral("SELECT id FROM tags WHERE normalized_name = :key"));
    existing.bindValue(QStringLiteral(":key"), normalizedName(name));
    if (!existing.exec()) { setError(existing.lastError().text()); return false; }
    if (existing.next() && existing.value(0).toInt() != id) {
        setError(QStringLiteral("Tag already exists"));
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE tags SET name = :name, normalized_name = :normalized_name WHERE id = :id"));
    query.bindValue(QStringLiteral(":name"), name.trimmed());
    query.bindValue(QStringLiteral(":normalized_name"), normalizedName(name));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) { setError(query.lastError().text()); return false; }
    if (query.numRowsAffected() == 0) { setError(QStringLiteral("Tag does not exist")); return false; }
    return true;
}

bool SongStore::deleteTag(int id)
{
    if (!m_ready || id <= 0) return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM tags WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) { setError(query.lastError().text()); return false; }
    if (query.numRowsAffected() == 0) { setError(QStringLiteral("Tag does not exist")); return false; }
    return true;
}

bool SongStore::replaceSongTags(int songId, const QStringList &names)
{
    QVector<QString> uniqueNames;
    QSet<QString> seen;
    for (const auto &name : names) {
        if (!validName(name)) {
            setError(QStringLiteral("Tag name must contain 1 to 64 characters"));
            return false;
        }
        const auto key = normalizedName(name);
        if (seen.contains(key)) continue;
        seen.insert(key);
        uniqueNames.append(name.trimmed());
    }

    QSqlQuery remove(m_db);
    remove.prepare(QStringLiteral("DELETE FROM song_tags WHERE song_id = :song_id"));
    remove.bindValue(QStringLiteral(":song_id"), songId);
    if (!remove.exec()) { setError(remove.lastError().text()); return false; }

    for (const auto &name : uniqueNames) {
        const auto key = normalizedName(name);
        QSqlQuery lookup(m_db);
        lookup.prepare(QStringLiteral("SELECT id FROM tags WHERE normalized_name = :key"));
        lookup.bindValue(QStringLiteral(":key"), key);
        if (!lookup.exec()) { setError(lookup.lastError().text()); return false; }
        int tagId = 0;
        if (lookup.next()) {
            tagId = lookup.value(0).toInt();
        } else {
            tagId = createTag(name);
            if (!tagId) return false;
        }
        QSqlQuery assign(m_db);
        assign.prepare(QStringLiteral("INSERT INTO song_tags (song_id, tag_id) VALUES (:song_id, :tag_id)"));
        assign.bindValue(QStringLiteral(":song_id"), songId);
        assign.bindValue(QStringLiteral(":tag_id"), tagId);
        if (!assign.exec()) { setError(assign.lastError().text()); return false; }
    }
    return true;
}

QVector<QString> SongStore::pathsForSong(int songId) const
{
    QVector<QString> paths;
    if (!m_ready || songId <= 0) return paths;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT path FROM queue_items WHERE song_id = :id ORDER BY position"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec()) while (query.next()) paths.append(query.value(0).toString());
    query.prepare(QStringLiteral(
        "SELECT path FROM playlist_items WHERE song_id = :id ORDER BY playlist_id, position"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec()) while (query.next()) paths.append(query.value(0).toString());
    query.prepare(QStringLiteral("SELECT path FROM song_paths WHERE song_id = :id ORDER BY rowid DESC"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec()) while (query.next()) paths.append(query.value(0).toString());
    query.prepare(QStringLiteral("SELECT first_path FROM songs WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec() && query.next()) paths.append(query.value(0).toString());
    return paths;
}

QHash<int, QVector<QString>> SongStore::songPaths() const
{
    QHash<int, QVector<QString>> paths;
    if (!m_ready) return paths;
    QSqlQuery query(m_db);
    if (query.exec(QStringLiteral("SELECT song_id, path FROM queue_items ORDER BY position"))) {
        while (query.next()) paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    if (query.exec(QStringLiteral(
            "SELECT song_id, path FROM playlist_items ORDER BY playlist_id, position"))) {
        while (query.next()) paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    if (query.exec(QStringLiteral("SELECT song_id, path FROM song_paths ORDER BY rowid DESC"))) {
        while (query.next()) paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    if (query.exec(QStringLiteral("SELECT id, first_path FROM songs ORDER BY id"))) {
        while (query.next()) paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    return paths;
}

} // namespace nekotune
