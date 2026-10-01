#include "storage/queue_repository.h"
#include <QSet>
#include <QSqlError>
namespace nekotune {
bool QueueRepository::saveQueue(const QueueSnapshot &snapshot) {
    if (!m_session.isReady()) {
        return false;
    }

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM queue_items"))) {
        setError(query.lastError().text());
        return false;
    }

    query.prepare(QStringLiteral("INSERT INTO queue_items (position, path, song_id, current_index) "
                                 "VALUES (:position, :path, :song_id, :current_index)"));
    for (int index = 0; index < snapshot.items.size(); ++index) {
        const auto &item = snapshot.items.at(index);
        query.bindValue(QStringLiteral(":position"), index);
        query.bindValue(QStringLiteral(":path"), item.path);
        query.bindValue(QStringLiteral(":song_id"), item.songId);
        query.bindValue(QStringLiteral(":current_index"), snapshot.currentIndex);
        if (!query.exec()) {
            setError(query.lastError().text());
            return false;
        }
    }

    return true;
}

QueueSnapshot QueueRepository::loadQueue() const {
    QueueSnapshot snapshot;
    if (!m_session.isReady()) {
        return snapshot;
    }

    QSqlQuery query(m_db);
    if (!query.exec(
            QStringLiteral("SELECT path, song_id, current_index FROM queue_items ORDER BY position ASC"))) {
        return snapshot;
    }

    while (query.next()) {
        snapshot.items.append({query.value(0).toString(), query.value(1).toInt()});
        snapshot.currentIndex = query.value(2).toInt();
    }
    if (snapshot.items.isEmpty()) {
        snapshot.currentIndex = -1;
    }
    return snapshot;
}

} // namespace nekotune
