#pragma once
#include "application/library/download_service.h"
#include "infrastructure/ai/ai_backend.h"
#include "infrastructure/library/import_executor.h"
#include "infrastructure/library/music_directory.h"
#include "infrastructure/lyrics/sidecar_store.h"
#include "runtime/library_scanner.h"
#include "infrastructure/playback/qt_playback_backend.h"
#include "ipc/api/api_context.h"
#include "ipc/ipc_server.h"
#include "infrastructure/kugou/kugou_music_service.h"
#include "storage/database_session.h"
#include "storage/playlist_repository.h"
#include "storage/queue_repository.h"
#include "storage/song_repository.h"
#include "storage/tag_repository.h"
namespace nekotune {
/// Composition and lifetime owner for one embedded or standalone backend instance.
/// Construction, service access and shutdown all occur on its backend thread.
/// Worker services own separate threads but return value snapshots to this session.
/// Member declaration order preserves dependency lifetimes during reverse destruction.
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
    // Declaration order is dependency order; reverse destruction keeps repositories' database alive.
    DatabaseSession m_database;
    MusicDirectory m_music;
    SongRepository m_songs;
    QueueRepository m_queueRepository;
    PlaylistRepository m_playlistRepository;
    TagRepository m_tagRepository;
    LibraryService m_library;
    AiBackend m_aiBackend;
    AiService m_ai{m_aiBackend, m_library, m_tagRepository};
    PlaylistService m_playlists;
    TagService m_tags;
    QueueService m_queue;
    QtPlaybackBackend m_audio;
    PlayerEngine m_player;
    LyricsController m_lyrics;
    CoverService m_covers;
    SidecarStore m_sidecars;
    ImportExecutor m_imports;
    KugouMusicService m_kugouBackend;
    KugouService m_kugou{m_kugouBackend};
    CollectionService m_collections;
    ApiContext m_api;
    IpcRouter m_router;
    LibraryScanner m_scanner;
    DownloadService m_downloads;
    IpcServer m_server;
    bool m_stopped = false;
};
} // namespace nekotune
