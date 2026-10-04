#pragma once

#include <QCryptographicHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace nekotune {

/// Library identity is the audio SHA-256; paths and user-edited metadata may change independently.
struct SongMetadata {
    int id = 0;
    QString hash;
    QString firstPath;
    QString customTitle;
    QString artist;
    QString lyrics;
    QString sourceName;
    qint64 durationMs = 0;
    QString providerId;
    QString providerTrackId;
    QString album;
    QString coverUrl;
    bool isRemote() const { return !providerId.isEmpty(); }
    QString resourceKey() const {
        return isRemote() ? QStringLiteral("remote-") +
                                QString::fromLatin1(QCryptographicHash::hash(providerId.toUtf8() + '\0' +
                                                                                 providerTrackId.toUtf8(),
                                                                             QCryptographicHash::Sha256)
                                                        .toHex())
                          : hash;
    }
};

/// Missing fields preserve stored values; present empty strings/lists explicitly clear them.
struct MetadataPatch {
    std::optional<QString> title;
    std::optional<QString> artist;
    std::optional<QString> lyrics;
    std::optional<QStringList> tags;
};

/// Value-only inspection result that may cross threads without sharing a database connection.
/// durationMs == 0 means probing did not obtain a duration, not that import must fail.
struct ImportedFile {
    QString path;
    QString hash;
    QString sourceName;
    qint64 durationMs = 0;
    /// Optional discovery origin; playback still uses path, never the reference document.
    QString managedReferencePath;
};

/// Persisted membership contains a song ID plus the chosen path.
/// The path records the playback source for this occurrence.
/// It can differ from the song's original firstPath after reimport.
/// Reconstruct runtime queue IDs when loading these records.
/// Do not use songId as an occurrence ID when duplicates exist.
struct QueueRecord {
    QString path;
    int songId = 0;
};

/// Items and currentIndex must be interpreted together.
/// -1 means no selected item, including a nonempty restored queue.
/// The index addresses persisted items before invalid records filter.
/// QueueService remaps it while reconstructing surviving records.
/// This snapshot describes selection, not decoder position/state.
/// Restart does not implicitly resume playback from this structure.
struct QueueSnapshot {
    QVector<QueueRecord> items;
    int currentIndex = -1;
};

/// The ordered item vector is the playlist's durable playback order.
/// Its identity and name belong to the collection, not a queue folder.
/// Playing it creates a queue replacement only after validation.
/// Editing its membership does not mutate an already playing queue.
struct Playlist {
    int id = 0;
    QString name;
    QVector<QueueRecord> items;
};

/// Persistent catalog identity with the stored display spelling.
/// Membership and filtering use id so renaming does not change assigned songs.
/// Name normalization for uniqueness belongs to the repository, not each consuming view.
struct SongTag {
    int id = 0;
    QString name;
};

} // namespace nekotune
