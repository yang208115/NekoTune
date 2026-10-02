#pragma once

#include "domain/library/library_types.h"

#include <QList>
#include <QString>

namespace nekotune {

/// A playback occurrence: id identifies this entry, metadata.id identifies the library song.
/// The same song may occur more than once, with independent navigation identities.
struct QueueItem {
    int id = 0;
    SongMetadata metadata;
    QString path;
    QString state;
};

/// In-memory queue only; currentIndex == -1 means no selection, even for a nonempty queue.
class PlayerQueue final {
  public:
    bool isEmpty() const;
    int size() const;
    int currentIndex() const;
    /// Accept only -1 or an index in the current item vector.
    /// Selection does not start decoding or persist any state.
    /// markCurrent() separately updates the presentation state strings.
    /// Callers prepare a candidate queue before committing it.
    bool setCurrentIndex(int index);

    /// Append a new occurrence without selecting or playing it.
    /// Allocate an occurrence ID even for an already queued song.
    /// The returned ID survives index changes within this runtime queue.
    /// Neither the file nor metadata is revalidated by this value object.
    /// Inspection and persistence remain application responsibilities.
    int add(const QString &path, const SongMetadata &metadata);
    /// Remove one occurrence, adjusting selection when earlier positions disappear.
    /// Removing the current occurrence leaves it unselected; the player decides continuation.
    /// Invalid indices fail without changing the candidate value object.
    bool removeAt(int index);
    /// Clear items and selection while retaining the occurrence-ID allocation sequence.
    /// Reusing an old ID could confuse navigation history after a collection replacement.
    /// Persistence and clearing the decoder source are separate application operations.
    void clear();
    void markCurrent();
    /// Replace metadata for every occurrence with the matching persistent song identity.
    /// Occurrence IDs and chosen playback paths remain unchanged.
    /// This updates duplicates consistently after library edits without rebuilding the queue.
    void updateSongMetadata(const SongMetadata &metadata);

    QueueItem &operator[](int index);
    const QueueItem &at(int index) const;
    int indexById(int queueId) const;

    QVector<QueueRecord> records() const;
    /// Restore complete runtime items, preserving their occurrence IDs.
    /// Advance the allocation counter beyond every restored identity.
    /// Invalid selected indices leave the queue without a current item.
    /// Refresh state strings so stale serialized marks cannot coexist.
    /// Database loading uses QueueRecord reconstruction separately.
    void restore(const QVector<QueueItem> &items, int currentIndex);

  private:
    QList<QueueItem> m_items;
    int m_currentIndex = -1;
    int m_nextQueueId = 1;
};

} // namespace nekotune
