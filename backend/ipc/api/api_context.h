#pragma once
#include "application/ai_service.h"
#include "application/playback/audio_output_service.h"
#include "application/library/collection_service.h"
#include "application/lyrics/cover_service.h"
#include "application/lyrics/lyrics_controller.h"
#include "application/library/tag_service.h"
#include "domain/library/file_inspector.h"
#include "ipc/ipc_router.h"
#include "ipc/serialization/lyrics_serialization.h"
#include "ipc/serialization/serialization.h"
namespace nekotune {
class ExtensionService;
class MusicService;
/// Non-owning service references used by feature route registration.
/// The composition root must outlive all captured handler references.
/// Snapshot helpers join domain data with transport-specific presentation.
/// withCover is the shared artwork enrichment boundary for every list.
/// Import executors provide values; the API never shares SQL with workers.
/// Optional runtime capabilities such as scanning are injected callbacks.
/// This aggregate does not own services or duplicate their business state.
struct ApiContext {
    PlayerEngine &player;
    QueueService &queue;
    LibraryService &library;
    PlaylistService &playlists;
    TagService &tags;
    CollectionService &collections;
    LyricsController &lyrics;
    IFileInspector &imports;
    QString databasePath;
    CoverService &covers;
    std::function<bool()> scan;
    QString musicDirectory;
    QString configDirectory;
    AiService *ai = nullptr;
    ExtensionService *extensions = nullptr;
    MusicService *music = nullptr;
    AudioOutputService *audioOutputs = nullptr;
    QJsonObject audioOutputStatus() const;
    QJsonObject playbackStatus() const;
    QJsonObject libraryStatus() const;
    QJsonArray queueItems() const;
    QJsonObject withCover(QJsonObject song) const;
    QJsonObject status() const;
    QJsonObject queueStatus() const;
    QJsonArray playlistList() const;
};
void registerPlayerApi(IpcRouter &, ApiContext &);
void registerLibraryApi(IpcRouter &, ApiContext &);
void registerLyricsApi(IpcRouter &, ApiContext &);
void registerAiApi(IpcRouter &, ApiContext &);
void registerExtensionsApi(IpcRouter &, ApiContext &);
void registerMusicApi(IpcRouter &, ApiContext &);
bool positiveId(const QJsonValue &value);
Result<int> requiredId(const QJsonObject &params, const QString &key);
} // namespace nekotune
