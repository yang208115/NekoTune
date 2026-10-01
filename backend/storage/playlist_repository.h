#pragma once
#include "domain/repositories.h"
#include "storage/database_session.h"
#include <QSqlQuery>
namespace nekotune {
class PlaylistRepository final : public IPlaylistRepository {
  public:
    explicit PlaylistRepository(DatabaseSession &session) : m_session(session), m_db(session.database()) {}
    QString errorString() const override { return m_session.errorString(); }
    QVector<Playlist> playlists() const override;
    std::optional<Playlist> playlistById(int id) const override;
    int createPlaylist(const QString &name) override;
    bool renamePlaylist(int id, const QString &name) override;
    bool deletePlaylist(int id) override;
    bool addPlaylistSong(int id, const QueueRecord &song) override;
    bool removePlaylistSong(int id, int songId) override;
    bool removeSongEverywhere(int songId) override;

  private:
    void setError(const QString &message) { m_session.setError(message); }
    DatabaseSession &m_session;
    QSqlDatabase m_db;
};
} // namespace nekotune
