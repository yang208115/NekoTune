#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace nekotune {

struct SongMetadata {
    int id = 0;
    QString hash;
    QString firstPath;
    QString customTitle;
    QString artist;
    QString lyrics;
};

struct MetadataPatch {
    std::optional<QString> title;
    std::optional<QString> artist;
    std::optional<QString> lyrics;
    std::optional<QStringList> tags;
};

struct ImportedFile {
    QString path;
    QString hash;
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
