#pragma once
#include "domain/playback/player_queue.h"
#include "domain/repositories.h"
#include "domain/result.h"
#include <QObject>
namespace nekotune {
/// Owns the authoritative in-memory queue alongside its persistence seam.
/// commit() writes a candidate before replacing the live value.
/// adoptCommitted() is for larger transactions that already saved it.
/// Using the latter outside such a commit would bypass durability.
/// Metadata refresh updates occurrences without altering membership.
/// changed() means consumers should reconcile their queue snapshot.
class QueueService final : public QObject {
    Q_OBJECT
  public:
    QueueService(IQueueRepository &repository, ISongRepository &songs, ITransaction &transaction);
    const PlayerQueue &queue() const { return m_queue; }
    Result<void> commit(PlayerQueue next);
    void adoptCommitted(PlayerQueue next);
    void updateMetadata(const SongMetadata &metadata);
  signals:
    void changed();

  private:
    IQueueRepository &m_repository;
    ITransaction &m_transaction;
    PlayerQueue m_queue;
};
} // namespace nekotune
