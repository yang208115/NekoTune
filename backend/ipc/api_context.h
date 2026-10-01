#pragma once
#include "application/collection_service.h"
#include "application/cover_service.h"
#include "application/kugou_service.h"
#include "application/lyrics_controller.h"
#include "application/tag_service.h"
#include "domain/file_inspector.h"
#include "ipc/ipc_router.h"
#include "ipc/kugou_serialization.h"
#include "ipc/lyrics_serialization.h"
#include "ipc/serialization.h"
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
bool positiveId(const QJsonValue &value);
Result<int> requiredId(const QJsonObject &params, const QString &key);
} // namespace nekotune
