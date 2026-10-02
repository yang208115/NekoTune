#include "application/library/playlist_service.h"
namespace nekotune {
Result<Playlist> PlaylistService::get(int id) const {
    auto result = m_playlists.playlistById(id);
    if (!result)
        return failure(QStringLiteral("Playlist does not exist"), ErrorCode::NotFound);
    return *result;
}
Result<int> PlaylistService::create(const QString &name) {
    auto id = m_playlists.createPlaylist(name);
    if (!id)
        return failure(m_playlists.errorString());
    emit changed();
    return id;
}
Result<void> PlaylistService::rename(int id, const QString &name) {
    if (!m_playlists.renamePlaylist(id, name))
        return failure(m_playlists.errorString());
    emit changed();
    return {};
}
Result<void> PlaylistService::remove(int id) {
    if (!m_playlists.deletePlaylist(id))
        return failure(m_playlists.errorString());
    emit changed();
    return {};
}
Result<void> PlaylistService::add(int id, const QueueRecord &song) {
    if (!get(id))
        return get(id).error();
    if (!m_playlists.addPlaylistSong(id, song))
        return failure(m_playlists.errorString());
    emit changed();
    return {};
}
Result<void> PlaylistService::removeSong(int id, int songId) {
    if (!m_playlists.removePlaylistSong(id, songId))
        return failure(m_playlists.errorString());
    emit changed();
    return {};
}
} // namespace nekotune
