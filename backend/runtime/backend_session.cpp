#include "runtime/backend_session.h"
#include "app_paths.h"
#include "infrastructure/lyrics_storage.h"
#include "lyrics/kugou_provider.h"
#include "lyrics/lrclib_provider.h"
namespace nekotune {
namespace {
LyricsService *createLyrics() {
    auto *lrclib = new LrclibProvider;
    auto *kugou = new KugouProvider;
    auto *service = new LyricsService({lrclib, kugou}, std::make_unique<LyricsStorage>());
    service->setOffline(AppPaths::setting("lyrics_offline").toBool());
    lrclib->setParent(service);
    kugou->setParent(service);
    return service;
}
} // namespace
BackendSession::BackendSession()
    : m_music(m_database), m_songs(m_database), m_queueRepository(m_database),
      m_playlistRepository(m_database), m_tagRepository(m_database),
      m_library(m_songs, m_tagRepository, m_database), m_playlists(m_playlistRepository),
      m_tags(m_tagRepository), m_queue(m_queueRepository, m_songs, m_database), m_player(m_audio, m_queue),
      m_lyrics(m_player, createLyrics()), m_covers(std::make_unique<LyricsStorage>()),
      m_kugouBackend(
          nullptr, nullptr, {}, {}, {}, {},
          [this](const QString &hash, const QString &title) { return m_music.reserveDownload(hash, title); }),
      m_collections(m_songs, m_queueRepository, m_playlistRepository, m_database, m_library, m_queue,
                    m_player),
      m_api{m_player,      m_queue,  m_library, m_playlists, m_tags,
            m_collections, m_lyrics, m_imports, m_kugou,     m_database.databasePath(),
            m_covers},
      m_scanner(m_imports, m_music, m_library, m_router.commands()),
      m_downloads(m_library, m_imports, m_router.commands()),
      m_server(m_router, [this] { return m_api.status(); }) {
    m_covers.updateLyrics(m_lyrics.snapshot());
    m_imports.setMapper([this](const ImportedFile &file) { return m_music.manage(file); });
    m_api.scan = [this] { return m_scanner.start(); };
    m_api.musicDirectory = m_music.directory();
    m_api.configDirectory = AppPaths::configDirectory();
    m_api.ai = &m_ai;
    connect(&m_scanner, &LibraryScanner::finished, this,
            [this](int imported, int skipped, const QStringList &errors) {
                m_server.broadcastEvent({{"event", "library.scan_finished"},
                                         {"imported", imported},
                                         {"skipped", skipped},
                                         {"failed", errors.size()},
                                         {"errors", QJsonArray::fromStringList(errors)}});
            });
    connect(&m_kugou, &KugouService::audioReady, &m_downloads, &DownloadService::importDownloaded);
    registerPlayerApi(m_router, m_api);
    registerLibraryApi(m_router, m_api);
    registerLyricsApi(m_router, m_api);
    registerKugouApi(m_router, m_api);
    registerAiApi(m_router, m_api);
    auto libraryChanged = [this] { m_server.broadcastEvent({{"event", "library.changed"}}); };
    auto playlistsChanged = [this] {
        m_server.broadcastEvent({{"event", "playlist.changed"}, {"playlists", m_api.playlistList()}});
    };
    connect(&m_library, &LibraryService::changed, this, libraryChanged);
    connect(&m_tags, &TagService::changed, this, libraryChanged);
    connect(&m_collections, &CollectionService::libraryChanged, this, libraryChanged);
    connect(&m_collections, &CollectionService::playlistsChanged, this, playlistsChanged);
    connect(&m_playlists, &PlaylistService::changed, this, playlistsChanged);
    connect(&m_library, &LibraryService::durationUpdated, &m_queue, &QueueService::updateMetadata);
    connect(&m_library, &LibraryService::durationUpdated, this,
            [playlistsChanged](const SongMetadata &) { playlistsChanged(); });
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
        m_sidecars.setCurrent(state.trackId, state.revision, state.offline);
        m_covers.updateLyrics(state);
        m_server.broadcastEvent({{"event", "lyrics.changed"}, {"lyrics", toJson(state)}});
    });
    connect(&m_lyrics, &LyricsController::assetsReady, this,
            [this](const QString &hash, const LyricsDocument &document, quint64 revision) {
                m_sidecars.save(hash, revision, m_music.baseFor(hash), document);
            });
    connect(&m_sidecars, &SidecarStore::saved, &m_covers, &CoverService::assetsUpdated);
    connect(&m_sidecars, &SidecarStore::failed, this, [this](const QString &hash, const QString &message) {
        m_server.broadcastEvent(
            {{"event", "library.assets_failed"}, {"song_hash", hash}, {"message", message}});
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
bool BackendSession::start() {
    if (!m_database.isReady() || !m_server.listen())
        return false;
    m_scanner.start();
    return true;
}
QString BackendSession::errorString() const {
    return m_database.isReady() ? m_server.errorString() : m_database.errorString();
}
void BackendSession::shutdown() {
    if (m_stopped)
        return;
    m_stopped = true;
    m_scanner.shutdown();
    m_sidecars.shutdown();
    m_server.stopAccepting();
    m_router.shutdown();
    m_aiBackend.shutdown();
    m_imports.shutdown();
    m_server.shutdown();
    m_kugouBackend.shutdown();
    m_lyrics.shutdown();
    m_player.shutdown();
}
} // namespace nekotune
