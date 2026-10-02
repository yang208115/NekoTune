#include "storage/queue_repository.h"
#include <QSet>
#include <QSqlError>
namespace nekotune {
// Replace the table within the caller's surrounding transaction.
// The delete/insert sequence must not be used as separate commits.
// Each row repeats the selected index for legacy schema compatibility.
// Persist positional order rather than runtime queue occurrence IDs.
// The application adopts memory only after its transaction commits.
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

// Restore ordered records even when their files are currently unavailable.
// QueueService later joins song metadata and filters stale references.
// The selected index is read from the legacy per-row representation.
// An empty table has no selected item regardless of default row values.
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
