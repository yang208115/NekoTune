#include "storage/tag_repository.h"
#include <QSet>
#include <QSqlError>
namespace nekotune {
namespace {

QString normalizedName(const QString &name) { return name.trimmed().toCaseFolded(); }

bool validName(const QString &name) {
    const auto trimmed = name.trimmed();
    return !trimmed.isEmpty() && trimmed.size() <= 64;
}

} // namespace

QVector<SongTag> TagRepository::tags() const {
    QVector<SongTag> result;
    if (!m_session.isReady())
        return result;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT id, name FROM tags ORDER BY normalized_name, id")))
        return result;
    while (query.next())
        result.append({query.value(0).toInt(), query.value(1).toString()});
    return result;
}

QHash<int, QVector<SongTag>> TagRepository::songTags() const {
    QHash<int, QVector<SongTag>> result;
    if (!m_session.isReady())
        return result;
    QSqlQuery query(m_db);
    if (!query.exec(
            QStringLiteral("SELECT st.song_id, t.id, t.name FROM song_tags st "
                           "JOIN tags t ON t.id = st.tag_id ORDER BY st.song_id, t.normalized_name, t.id")))
        return result;
    while (query.next())
        result[query.value(0).toInt()].append({query.value(1).toInt(), query.value(2).toString()});
    return result;
}

// Catalog creation rejects an existing normalized name instead of silently returning its ID.
// Assignment replacement below may intentionally reuse that ID under a different workflow.
// The distinction preserves useful feedback when the user explicitly creates a duplicate tag.
int TagRepository::createTag(const QString &name) {
    if (!m_session.isReady())
        return 0;
    if (!validName(name)) {
        setError(QStringLiteral("Tag name must contain 1 to 64 characters"));
        return 0;
    }
    QSqlQuery existing(m_db);
    existing.prepare(QStringLiteral("SELECT id FROM tags WHERE normalized_name = :key"));
    existing.bindValue(QStringLiteral(":key"), normalizedName(name));
    if (!existing.exec()) {
        setError(existing.lastError().text());
        return 0;
    }
    if (existing.next()) {
        setError(QStringLiteral("Tag already exists"));
        return 0;
    }
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("INSERT INTO tags (name, normalized_name) VALUES (:name, :normalized_name)"));
    query.bindValue(QStringLiteral(":name"), name.trimmed());
    query.bindValue(QStringLiteral(":normalized_name"), normalizedName(name));
    if (!query.exec()) {
        setError(query.lastError().text());
        return 0;
    }
    return query.lastInsertId().toInt();
}

// Case-only renames may reuse this tag's own normalized identity.
// A collision with another tag ID still rejects the operation.
// Updating the label retains every song_tags association by ID.
// Validate lengths and uniqueness before publishing the global change.
bool TagRepository::renameTag(int id, const QString &name) {
    if (!m_session.isReady() || id <= 0)
        return false;
    if (!validName(name)) {
        setError(QStringLiteral("Tag name must contain 1 to 64 characters"));
        return false;
    }
    QSqlQuery existing(m_db);
    existing.prepare(QStringLiteral("SELECT id FROM tags WHERE normalized_name = :key"));
    existing.bindValue(QStringLiteral(":key"), normalizedName(name));
    if (!existing.exec()) {
        setError(existing.lastError().text());
        return false;
    }
    if (existing.next() && existing.value(0).toInt() != id) {
        setError(QStringLiteral("Tag already exists"));
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("UPDATE tags SET name = :name, normalized_name = :normalized_name WHERE id = :id"));
    query.bindValue(QStringLiteral(":name"), name.trimmed());
    query.bindValue(QStringLiteral(":normalized_name"), normalizedName(name));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        setError(QStringLiteral("Tag does not exist"));
        return false;
    }
    return true;
}

bool TagRepository::deleteTag(int id) {
    if (!m_session.isReady() || id <= 0)
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM tags WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        setError(QStringLiteral("Tag does not exist"));
        return false;
    }
    return true;
}

bool TagRepository::replaceSongTags(int songId, const QStringList &names) {
    // Validate the full batch before deleting associations; the caller owns the surrounding transaction.
    // Case-folded names reuse one tag identity while preserving its existing display spelling.
    QVector<QString> uniqueNames;
    QSet<QString> seen;
    for (const auto &name : names) {
        if (!validName(name)) {
            setError(QStringLiteral("Tag name must contain 1 to 64 characters"));
            return false;
        }
        const auto key = normalizedName(name);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        uniqueNames.append(name.trimmed());
    }

    // The names have been validated and deduplicated before changing membership.
    // This delete-and-rebuild sequence requires the application's surrounding metadata transaction.
    // A later tag creation or assignment failure must roll back these removed associations too.
    QSqlQuery remove(m_db);
    remove.prepare(QStringLiteral("DELETE FROM song_tags WHERE song_id = :song_id"));
    remove.bindValue(QStringLiteral(":song_id"), songId);
    if (!remove.exec()) {
        setError(remove.lastError().text());
        return false;
    }

    for (const auto &name : uniqueNames) {
        const auto key = normalizedName(name);
        QSqlQuery lookup(m_db);
        lookup.prepare(QStringLiteral("SELECT id FROM tags WHERE normalized_name = :key"));
        lookup.bindValue(QStringLiteral(":key"), key);
        if (!lookup.exec()) {
            setError(lookup.lastError().text());
            return false;
        }
        int tagId = 0;
        if (lookup.next()) {
            tagId = lookup.value(0).toInt();
        } else {
            tagId = createTag(name);
            if (!tagId)
                return false;
        }
        QSqlQuery assign(m_db);
        assign.prepare(QStringLiteral("INSERT INTO song_tags (song_id, tag_id) VALUES (:song_id, :tag_id)"));
        assign.bindValue(QStringLiteral(":song_id"), songId);
        assign.bindValue(QStringLiteral(":tag_id"), tagId);
        if (!assign.exec()) {
            setError(assign.lastError().text());
            return false;
        }
    }
    return true;
}

} // namespace nekotune
