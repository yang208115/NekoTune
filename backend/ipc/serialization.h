#pragma once
#include "application/library_service.h"
#include "core/player_engine.h"
#include <QJsonArray>
#include <QJsonObject>
namespace nekotune {
QJsonObject toJson(const SongMetadata &song);
QJsonObject toJson(const QueueItem &item, int position = -1, bool includeLyrics = false);
QJsonArray queueJson(const PlayerQueue &queue);
QJsonObject currentSongJson(const PlayerQueue &queue);
QJsonArray toJson(const QVector<SongTag> &tags);
QJsonObject toJson(const LibrarySnapshot &library);
QJsonObject toJson(const PlaybackSnapshot &state);
QJsonObject success(const QJsonObject &data = {});
QJsonObject error(const AppError &error);
QJsonObject response(const Result<void> &result, const QJsonObject &data = {});
} // namespace nekotune
