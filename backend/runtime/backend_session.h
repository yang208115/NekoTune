#pragma once
#include "application/download_service.h"
#include "infrastructure/import_executor.h"
#include "infrastructure/qt_playback_backend.h"
#include "ipc/api_context.h"
#include "ipc/ipc_server.h"
#include "kugou/kugou_music_service.h"
#include "storage/database_session.h"
#include "storage/playlist_repository.h"
#include "storage/queue_repository.h"
#include "storage/song_repository.h"
#include "storage/tag_repository.h"
namespace nekotune {
class BackendSession final : public QObject {
    Q_OBJECT
  public:
    BackendSession();
    ~BackendSession() override;
    bool start();
    QString errorString() const;
    QString serverName() const { return m_server.serverName(); }
    void shutdown();

  private:
    DatabaseSession m_database;
    SongRepository m_songs;
    QueueRepository m_queueRepository;
    PlaylistRepository m_playlistRepository;
    TagRepository m_tagRepository;
    LibraryService m_library;
    PlaylistService m_playlists;
    TagService m_tags;
    QueueService m_queue;
    QtPlaybackBackend m_audio;
    PlayerEngine m_player;
    LyricsController m_lyrics;
    CoverService m_covers;
    ImportExecutor m_imports;
    KugouMusicService m_kugouBackend;
    KugouService m_kugou{m_kugouBackend};
    CollectionService m_collections;
    ApiContext m_api;
    IpcRouter m_router;
    DownloadService m_downloads;
    IpcServer m_server;
    bool m_stopped = false;
};
} // namespace nekotune
