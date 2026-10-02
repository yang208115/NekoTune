#pragma once

#include "domain/library/library_types.h"

#include <QList>
#include <QString>

namespace nekotune {

struct QueueItem {
    int id = 0;
    SongMetadata metadata;
    QString path;
    QString state;
};

class PlayerQueue final {
  public:
    bool isEmpty() const;
    int size() const;
    int currentIndex() const;
    bool setCurrentIndex(int index);

    int add(const QString &path, const SongMetadata &metadata);
    bool removeAt(int index);
    void clear();
    void markCurrent();
    void updateSongMetadata(const SongMetadata &metadata);

    QueueItem &operator[](int index);
    const QueueItem &at(int index) const;
    int indexById(int queueId) const;

    QVector<QueueRecord> records() const;
    void restore(const QVector<QueueItem> &items, int currentIndex);

  private:
    QList<QueueItem> m_items;
    int m_currentIndex = -1;
    int m_nextQueueId = 1;
};

} // namespace nekotune
