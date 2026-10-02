#include "application/playback/queue_service.h"
#include "application/transaction.h"
namespace nekotune {
QueueService::QueueService(IQueueRepository &repository, ISongRepository &songs, ITransaction &transaction)
    : m_repository(repository), m_transaction(transaction) {
    const auto snapshot = repository.loadQueue();
    int current = -1;
    for (int i = 0; i < snapshot.items.size(); ++i) {
        const auto &record = snapshot.items[i];
        auto song = songs.songById(record.songId);
        if (!song || record.path.isEmpty())
            continue;
        if (i == snapshot.currentIndex)
            current = m_queue.size();
        m_queue.add(record.path, *song);
    }
    m_queue.setCurrentIndex(current);
    m_queue.markCurrent();
}
Result<void> QueueService::commit(PlayerQueue next) {
    next.markCurrent();
    Transaction tx(m_transaction);
    if (!tx || !m_repository.saveQueue({next.records(), next.currentIndex()}) || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    adoptCommitted(std::move(next));
    return {};
}
void QueueService::adoptCommitted(PlayerQueue next) {
    next.markCurrent();
    m_queue = std::move(next);
    emit changed();
}
void QueueService::updateMetadata(const SongMetadata &metadata) {
    m_queue.updateSongMetadata(metadata);
    emit changed();
}
} // namespace nekotune
