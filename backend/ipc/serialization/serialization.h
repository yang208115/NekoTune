#pragma once
#include "application/library/library_service.h"
#include "application/playback/player_engine.h"
#include <QJsonArray>
#include <QJsonObject>
namespace nekotune {
QJsonObject toJson(const SongMetadata &song);
/// Serialize both queue occurrence identity and persistent library song identity.
/// @param position Include row state/order when nonnegative; omit them for current-song detail.
/// @param includeLyrics Include full custom lyrics only for explicit detail consumers.
/// Cover resolution is added by ApiContext after these domain values are serialized.
QJsonObject toJson(const QueueItem &item, int position = -1, bool includeLyrics = false);
QJsonArray queueJson(const PlayerQueue &queue);
/// Return an empty object for no selected occurrence, even when the queue is nonempty.
/// Selected detail includes lyrics that ordinary queue rows intentionally omit.
/// This shape allows frontend state to distinguish clearing a song from omitting a patch field.
QJsonObject currentSongJson(const PlayerQueue &queue);
QJsonArray toJson(const QVector<SongTag> &tags);
QJsonObject toJson(const LibrarySnapshot &library);
/// Combine decoder timing and merged display metadata without mutating library values.
/// Current-song title/artist use the engine's decoder/custom-field precedence.
/// No selected song serializes as an explicit empty object rather than a historical row.
QJsonObject toJson(const PlaybackSnapshot &state);
QJsonObject success(const QJsonObject &data = {});
QJsonObject error(const AppError &error);
QJsonObject response(const Result<void> &result, const QJsonObject &data = {});
} // namespace nekotune
