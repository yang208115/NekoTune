#pragma once
#include "domain/repositories.h"
#include "storage/database_session.h"
#include <QSqlQuery>
namespace nekotune {
class SongRepository final : public ISongRepository {
  public:
    explicit SongRepository(DatabaseSession &session) : m_session(session), m_db(session.database()) {}
    QString errorString() const override { return m_session.errorString(); }
    std::optional<SongMetadata> getOrCreateSong(const QString &hash, const QString &path,
                                                const QString &customTitle = {},
                                                const QString &artist = {},
                                                const QString &sourceName = {}, qint64 durationMs = 0) override;
    QVector<SongMetadata> songs() const override;
    std::optional<SongMetadata> getOrCreateRemote(const SongMetadata &) override;
    std::optional<SongMetadata> songById(int songId) const override;
    std::optional<SongMetadata> songByHash(const QString &hash) const override;
    std::optional<SongMetadata> updateMetadata(int songId, const QString &customTitle, const QString &artist,
                                               const QString &lyrics) override;
    QVector<QString> pathsForSong(int songId) const override;
    QHash<int, QVector<QString>> songPaths() const override;
    bool updateDuration(int songId, qint64 durationMs) override;
    bool erase(int songId) override;

  private:
    void setError(const QString &message) { m_session.setError(message); }
    bool rememberSongPath(int songId, const QString &path);
    std::optional<SongMetadata> readSongFromQuery(QSqlQuery &query) const;
    DatabaseSession &m_session;
    QSqlDatabase m_db;
};
} // namespace nekotune
