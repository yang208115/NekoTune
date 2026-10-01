#pragma once

#include <QString>
#include <QVector>

namespace nekotune {

struct SongMetadata {
    int id = 0;
    QString hash;
    QString firstPath;
    QString customTitle;
    QString artist;
    QString lyrics;
};

struct QueueRecord {
    QString path;
    int songId = 0;
};

struct QueueSnapshot {
    QVector<QueueRecord> items;
    int currentIndex = -1;
};

struct Playlist {
    int id = 0;
    QString name;
    QVector<QueueRecord> items;
};

struct SongTag {
    int id = 0;
    QString name;
};

} // namespace nekotune
