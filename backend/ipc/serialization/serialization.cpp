#include "ipc/serialization/serialization.h"
#include <QFileInfo>
namespace nekotune {
QJsonObject toJson(const SongMetadata &song) {
    return {{"song_id", song.id},           {"song_hash", song.hash},
            {"first_path", song.firstPath}, {"custom_title", song.customTitle},
            {"artist", song.artist},        {"lyrics", song.lyrics},
            {"duration_ms", song.durationMs}};
}
QJsonObject toJson(const QueueItem &item, int position, bool includeLyrics) {
    auto result = toJson(item.metadata);
    if (!includeLyrics)
        result.remove("lyrics");
    result.insert("id", item.id);
    result.insert("queue_id", item.id);
    result.insert("path", item.path);
    result.insert("title", item.metadata.customTitle.trimmed().isEmpty()
                               ? (item.metadata.sourceName.isEmpty() ? QFileInfo(item.path).completeBaseName()
                                                                     : item.metadata.sourceName)
                               : item.metadata.customTitle.trimmed());
    if (position >= 0) {
        result.insert("position", position);
        result.insert("state", item.state);
    }
    return result;
}
QJsonArray queueJson(const PlayerQueue &queue) {
    QJsonArray items;
    for (int i = 0; i < queue.size(); ++i)
        items.append(toJson(queue.at(i), i));
    return items;
}
QJsonObject currentSongJson(const PlayerQueue &queue) {
    return queue.currentIndex() < 0 ? QJsonObject{} : toJson(queue.at(queue.currentIndex()), -1, true);
}
QJsonArray toJson(const QVector<SongTag> &tags) {
    QJsonArray result;
    for (const auto &tag : tags)
        result.append(QJsonObject{{"id", tag.id}, {"name", tag.name}});
    return result;
}
QJsonObject toJson(const LibrarySnapshot &library) {
    QJsonArray songs;
    for (const auto &song : library.songs) {
        auto item = toJson(song.metadata);
        item.remove("lyrics");
        item.insert("path", song.path);
        item.insert("available", !song.path.isEmpty());
        item.insert("title", song.metadata.customTitle.trimmed().isEmpty()
                                 ? (song.metadata.sourceName.isEmpty()
                                        ? QFileInfo(song.path.isEmpty() ? song.metadata.firstPath : song.path)
                                              .completeBaseName()
                                        : song.metadata.sourceName)
                                 : song.metadata.customTitle.trimmed());
        item.insert("tags", toJson(song.tags));
        songs.append(item);
    }
    return {{"songs", songs}, {"tags", toJson(library.tags)}};
}
QJsonObject toJson(const PlaybackSnapshot &state) {
    QJsonObject song;
    if (state.song) {
        song = toJson(*state.song, -1, true);
        song.insert("title", state.metadata.title);
        song.insert("artist", state.metadata.artist);
        song.insert("album", state.metadata.album);
    }
    return {{"playback_mode", toString(state.playbackMode)},
            {"state", toString(state.state)},
            {"position", state.position},
            {"duration", state.duration},
            {"volume", state.volume},
            {"song", song}};
}
QJsonObject success(const QJsonObject &data) { return {{"status", "ok"}, {"data", data}}; }
QJsonObject error(const AppError &problem) { return {{"status", "error"}, {"message", problem.message}}; }
QJsonObject response(const Result<void> &result, const QJsonObject &data) {
    return result ? success(data) : error(result.error());
}
} // namespace nekotune
