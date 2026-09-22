#pragma once

#include <QString>
#include <QJsonArray>
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
    int folderId = 0;
};

struct QueueSnapshot {
    QVector<QueueRecord> items;
    int currentIndex = -1;
    QJsonArray folders;
};

} // namespace nekotune
