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

    static QString defaultDatabasePath();

private:
    bool initialize(const QString &databasePath);
    bool migrate();
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
