#pragma once
#include "domain/repositories.h"
#include "domain/result.h"
#include <QObject>
namespace nekotune {
/// Provides playlist management without owning playback state.
/// Creating, renaming and deleting collections does not select music.
/// Library-wide deletion uses CollectionService for cross-table changes.
/// The repository remains responsible for membership and name validation.
/// Observers refresh only after an accepted repository operation.
class PlaylistService final : public QObject {
    Q_OBJECT
  public:
    explicit PlaylistService(IPlaylistRepository &playlists) : m_playlists(playlists) {}
    QVector<Playlist> list() const { return m_playlists.playlists(); }
    Result<Playlist> get(int id) const;
    Result<int> create(const QString &name);
    Result<void> rename(int id, const QString &name);
    Result<void> remove(int id);
    Result<void> add(int id, const QueueRecord &song);
    Result<void> removeSong(int id, int songId);
  signals:
    void changed();

  private:
    IPlaylistRepository &m_playlists;
};
} // namespace nekotune
