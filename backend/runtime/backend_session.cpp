#include "runtime/backend_session.h"
#include "infrastructure/lyrics_storage.h"
#include "lyrics/kugou_provider.h"
#include "lyrics/lrclib_provider.h"
namespace nekotune {
namespace {
LyricsService *createLyrics() {
    auto *lrclib = new LrclibProvider;
    auto *kugou = new KugouProvider;
    auto *service = new LyricsService({lrclib, kugou}, std::make_unique<LyricsStorage>());
    lrclib->setParent(service);
    kugou->setParent(service);
    return service;
}
} // namespace
BackendSession::BackendSession()
    : m_songs(m_database), m_queueRepository(m_database), m_playlistRepository(m_database),
      m_tagRepository(m_database), m_library(m_songs, m_tagRepository, m_database),
      m_playlists(m_playlistRepository), m_tags(m_tagRepository),
      m_queue(m_queueRepository, m_songs, m_database), m_player(m_audio, m_queue),
      m_lyrics(m_player, createLyrics()), m_covers(std::make_unique<LyricsStorage>()),
      m_collections(m_songs, m_queueRepository, m_playlistRepository, m_database, m_library, m_queue,
                    m_player),
      m_api{m_player,      m_queue,  m_library, m_playlists, m_tags,
            m_collections, m_lyrics, m_imports, m_kugou,     m_database.databasePath(),
            m_covers},
      m_downloads(m_library, m_imports, m_router.commands()),
      m_server(m_router, [this] { return m_api.status(); }) {
    connect(&m_kugou, &KugouService::audioReady, &m_downloads, &DownloadService::importDownloaded);
    registerPlayerApi(m_router, m_api);
    registerLibraryApi(m_router, m_api);
    registerLyricsApi(m_router, m_api);
    registerKugouApi(m_router, m_api);
    auto libraryChanged = [this] { m_server.broadcastEvent({{"event", "library.changed"}}); };
    auto playlistsChanged = [this] {
        m_server.broadcastEvent({{"event", "playlist.changed"}, {"playlists", m_api.playlistList()}});
    };
    connect(&m_library, &LibraryService::changed, this, libraryChanged);
    connect(&m_tags, &TagService::changed, this, libraryChanged);
    connect(&m_collections, &CollectionService::libraryChanged, this, libraryChanged);
    connect(&m_collections, &CollectionService::playlistsChanged, this, playlistsChanged);
    connect(&m_playlists, &PlaylistService::changed, this, playlistsChanged);
    connect(&m_library, &LibraryService::metadataChanged, &m_player, &PlayerEngine::metadataUpdated);
    connect(&m_library, &LibraryService::metadataChanged, this,
            [playlistsChanged](const SongMetadata &) { playlistsChanged(); });
    auto queueChanged = [this] {
        m_server.broadcastEvent({{"event", "queue.changed"}, {"queue", m_api.queueItems()}});
    };
    connect(&m_queue, &QueueService::changed, this, queueChanged);
    connect(&m_player, &PlayerEngine::stateChanged, this, [this](PlayerState state) {
        m_server.broadcastEvent({{"event", "player.state_changed"}, {"state", toString(state)}});
    });
    connect(&m_player, &PlayerEngine::positionChanged, this, [this](qint64 position, qint64 duration) {
        m_server.broadcastEvent(
            {{"event", "player.position_changed"}, {"position", position}, {"duration", duration}});
    });
    connect(&m_player, &PlayerEngine::durationChanged, this, [this](qint64 duration) {
        m_server.broadcastEvent({{"event", "player.duration_changed"}, {"duration", duration}});
    });
    connect(&m_player, &PlayerEngine::volumeChanged, this, [this](double volume) {
        m_server.broadcastEvent({{"event", "player.volume_changed"}, {"volume", volume}});
    });
    auto trackChanged = [this] {
        m_server.broadcastEvent(
            {{"event", "player.track_changed"}, {"song", m_api.playbackStatus().value("song")}});
    };
    connect(&m_player, &PlayerEngine::trackChanged, this, trackChanged);
    connect(&m_player, &PlayerEngine::errorOccurred, this, [this](const QString &message) {
        m_server.broadcastEvent({{"event", "player.error"}, {"message", message}});
    });
    connect(&m_lyrics, &LyricsController::changed, this, [this](const LyricsSnapshot &state) {
        m_covers.updateLyrics(state);
        m_server.broadcastEvent({{"event", "lyrics.changed"}, {"lyrics", toJson(state)}});
    });
    connect(&m_covers, &CoverService::changed, this,
            [libraryChanged, playlistsChanged, queueChanged, trackChanged] {
                trackChanged();
                libraryChanged();
                playlistsChanged();
                queueChanged();
            });
    connect(&m_kugou, &KugouService::eventReady, this,
            [this](const KugouEvent &event) { m_server.broadcastEvent(toJson(event)); });
    connect(&m_downloads, &DownloadService::finished, this,
            [this](const QString &path, int id, const QString &lyric, const QString &cover) {
                m_server.broadcastEvent({{"event", "kugou.download_finished"},
                                         {"path", path},
                                         {"song_id", id},
                                         {"lyric_status", lyric},
                                         {"cover_status", cover}});
            });
    connect(&m_downloads, &DownloadService::importFailed, this, [this](const QString &path) {
        m_server.broadcastEvent({{"event", "kugou.operation_failed"},
                                 {"message", "Audio saved but library import failed"},
                                 {"path", path}});
    });
}
BackendSession::~BackendSession() { shutdown(); }
bool BackendSession::start() { return m_database.isReady() && m_server.listen(); }
QString BackendSession::errorString() const {
    return m_database.isReady() ? m_server.errorString() : m_database.errorString();
}
void BackendSession::shutdown() {
    if (m_stopped)
        return;
    m_stopped = true;
    m_server.stopAccepting();
    m_router.shutdown();
    m_imports.shutdown();
    m_server.shutdown();
    m_kugouBackend.shutdown();
    m_lyrics.shutdown();
    m_player.shutdown();
}
} // namespace nekotune
