#include "runtime/backend_session.h"
#include "app_paths.h"
#include "infrastructure/lyrics/lyrics_storage.h"
#include "infrastructure/lyrics/kugou_provider.h"
#include "infrastructure/lyrics/lrclib_provider.h"
namespace nekotune {
namespace {
// Providers are parented before the service is moved to its worker thread.
// That move carries their child network objects into the same thread affinity.
// The controller then owns service shutdown and thread joining as one lifetime unit.
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
// This is the composition root for both desktop and split operation.
// Repositories share the one database session created on this thread.
// Application services receive interfaces, not global adapter lookups.
// IPC routes and event wiring are registered once for this session.
// Artwork enrichment is centralized before snapshots reach any view.
// Worker adapters exchange value snapshots with this backend thread.
// Keep new feature wiring here rather than growing PlayerEngine.
BackendSession::BackendSession()
    : m_music(m_database), m_songs(m_database), m_queueRepository(m_database),
      m_playlistRepository(m_database), m_tagRepository(m_database),
      m_library(m_songs, m_tagRepository, m_database), m_playlists(m_playlistRepository),
      m_tags(m_tagRepository), m_queue(m_queueRepository, m_songs, m_database),
      m_player(m_audio, m_queue, std::make_unique<ShuffleBagStrategy>(),
               playbackModeFromString(AppPaths::setting("playback_mode").toString())
                   .value_or(PlaybackMode::Sequential),
               [](PlaybackMode mode) { return AppPaths::saveSetting("playback_mode", toString(mode)); }),
      m_lyrics(m_player, createLyrics()), m_covers(std::make_unique<LyricsStorage>()),
      m_kugouBackend(
          nullptr, nullptr, {}, {}, {}, {},
          [this](const QString &hash, const QString &title) { return m_music.reserveDownload(hash, title); }),
      m_collections(m_songs, m_queueRepository, m_playlistRepository, m_database, m_library, m_queue,
                    m_player, &m_music),
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
    // Duration enrichment updates every queued occurrence through library identity.
    // Playlist snapshots are also invalidated so all views share the same stored duration.
    // Neither notification selects another track or rebuilds collection membership.
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
    connect(&m_player, &PlayerEngine::playbackModeChanged, this, [this](PlaybackMode mode) {
        m_server.broadcastEvent({{"event", "player.playback_mode_changed"},
                                 {"playback_mode", toString(mode)}});
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
    // Update the sidecar's accepted identity before handling later assetsReady notifications.
    // The same lyric snapshot also updates cover resolution for every collection view.
    // Request revisions protect both presentation and eventual filesystem publication.
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
    // Artwork changes affect current track, library, playlists and repeated queue occurrences.
    // Broadcast all their existing snapshot channels instead of giving each view a resolver.
    // This keeps local/offline cover precedence consistent even when only one asset was updated.
    connect(&m_covers, &CoverService::changed, this,
            [libraryChanged, playlistsChanged, queueChanged, trackChanged] {
                trackChanged();
                libraryChanged();
                playlistsChanged();
                queueChanged();
            });
    connect(&m_kugou, &KugouService::eventReady, this,
            [this](const KugouEvent &event) { m_server.broadcastEvent(toJson(event)); });
    // Publish download_finished only after the application import transaction has succeeded.
    // The supplied path can be the managed/deduplicated path rather than the raw download target.
    // Lyric and cover statuses preserve useful partial success from optional enrichment.
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
// Database readiness and socket ownership are startup prerequisites.
// Only a successfully listening backend starts automatic discovery.
// The frontend also requests a scan on reconnect; scanner admission
// coalesces that request with this already-active startup scan.
// Listening success does not imply that discovery has finished yet.
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
    // Reject new work, then cancel queued commands before import shutdown completes active callbacks.
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
