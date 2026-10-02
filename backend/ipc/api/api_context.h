#pragma once
#include "application/ai_service.h"
#include "application/library/collection_service.h"
#include "application/lyrics/cover_service.h"
#include "application/kugou_service.h"
#include "application/lyrics/lyrics_controller.h"
#include "application/library/tag_service.h"
#include "domain/library/file_inspector.h"
#include "ipc/ipc_router.h"
#include "ipc/serialization/kugou_serialization.h"
#include "ipc/serialization/lyrics_serialization.h"
#include "ipc/serialization/serialization.h"
namespace nekotune {
struct ApiContext {
    PlayerEngine &player;
    QueueService &queue;
    LibraryService &library;
    PlaylistService &playlists;
    TagService &tags;
    CollectionService &collections;
    LyricsController &lyrics;
    IFileInspector &imports;
    KugouService &kugou;
    QString databasePath;
    CoverService &covers;
    std::function<bool()> scan;
    QString musicDirectory;
    QString configDirectory;
    AiService *ai = nullptr;
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
void registerKugouApi(IpcRouter &, ApiContext &);
void registerAiApi(IpcRouter &, ApiContext &);
bool positiveId(const QJsonValue &value);
Result<int> requiredId(const QJsonObject &params, const QString &key);
} // namespace nekotune
