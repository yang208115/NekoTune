#pragma once

#include "storage/song_metadata.h"

#include <QSqlDatabase>
#include <QVector>
#include <QString>

#include <optional>

namespace nekotune {

class SongStore final {
public:
    explicit SongStore(const QString &databasePath = defaultDatabasePath(),
                       const QString &connectionName = {});
    ~SongStore();

    SongStore(const SongStore &) = delete;
    SongStore &operator=(const SongStore &) = delete;

    bool isReady() const;
    QString errorString() const;
    QString databasePath() const;

    std::optional<SongMetadata> getOrCreateSong(const QString &hash,
                                                const QString &path);
    QVector<SongMetadata> songs() const;
    std::optional<SongMetadata> songById(int songId) const;
    std::optional<SongMetadata> updateMetadata(int songId,
                                               const QString &customTitle,
                                               const QString &artist,
                                               const QString &lyrics);
    bool saveAsrTask(const QString &taskId, const QString &songHash, const QString &model, const QString &language);
    bool saveTranscription(const QString &hash, const QByteArray &json);
    QByteArray transcription(const QString &hash) const;
    bool saveQueue(const QueueSnapshot &snapshot);
    QueueSnapshot loadQueue() const;
    QVector<Playlist> playlists() const;
    std::optional<Playlist> playlistById(int id) const;
    int createPlaylist(const QString &name);
    bool renamePlaylist(int id, const QString &name);
    bool deletePlaylist(int id);
    bool addPlaylistSong(int id, const QueueRecord &song);
    bool removePlaylistSong(int id, int songId);

    static QString defaultDatabasePath();

private:
    bool initialize(const QString &databasePath);
    bool migrate();
    bool migrateFolders();
    std::optional<SongMetadata> songByHash(const QString &hash) const;
    std::optional<SongMetadata> readSongFromQuery(QSqlQuery &query) const;
    void setError(const QString &message);

    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_databasePath;
    QString m_error;
    bool m_ready = false;
};

} // namespace nekotune
