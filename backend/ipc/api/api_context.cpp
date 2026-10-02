#include "ipc/api/api_context.h"
#include <QFileInfo>
namespace nekotune {
QJsonObject ApiContext::withCover(QJsonObject song) const {
    if (!song.isEmpty()) {
        const auto path = song.value("path").toString().isEmpty() ? song.value("first_path").toString()
                                                                  : song.value("path").toString();
        song.insert("cover_url", covers.resolve(path, song.value("song_hash").toString()));
    }
    return song;
}
QJsonObject ApiContext::playbackStatus() const {
    auto state = toJson(player.snapshot());
    state.insert("song", withCover(state.value("song").toObject()));
    return state;
}
QJsonObject ApiContext::libraryStatus() const {
    auto state = toJson(library.snapshot());
    QJsonArray songs;
    for (const auto &song : state.value("songs").toArray())
        songs.append(withCover(song.toObject()));
    state.insert("songs", songs);
    return state;
}
QJsonArray ApiContext::queueItems() const {
    QJsonArray items;
    for (const auto &item : queueJson(queue.queue()))
        items.append(withCover(item.toObject()));
    return items;
}
QJsonObject ApiContext::status() const {
    auto state = playbackStatus();
    state.insert("queue", queueItems());
    state.insert("playlists", playlistList());
    state.insert("database_path", databasePath);
    state.insert("music_directory", musicDirectory);
    state.insert("config_directory", configDirectory);
    state.insert("lyrics", toJson(lyrics.snapshot()));
    state.insert("kugou", toJson(kugou.status()));
    return state;
}
QJsonObject ApiContext::queueStatus() const {
    return {{"current_index", queue.queue().currentIndex()}, {"items", queueItems()}};
}
// Playlist rows reuse library metadata while retaining their chosen path.
// Skip references whose library song is no longer available in storage.
// Omit full lyrics to keep collection snapshots compact.
// Prefer human source names when managed filenames are only numbers.
// Resolve covers here so playlist artwork matches library/player artwork.
QJsonArray ApiContext::playlistList() const {
    QJsonArray result;
    for (const auto &playlist : playlists.list()) {
        QJsonArray items;
        for (const auto &record : playlist.items) {
            auto song = library.metadata(record.songId);
            if (!song)
                continue;
            auto item = toJson(song.value());
            item.remove("lyrics");
            item.insert("path", record.path);
            item.insert("title",
                        song.value().customTitle.trimmed().isEmpty()
                            ? (song.value().sourceName.isEmpty() ? QFileInfo(record.path).completeBaseName()
                                                                 : song.value().sourceName)
                            : song.value().customTitle.trimmed());
            items.append(withCover(item));
        }
        result.append(QJsonObject{{"id", playlist.id}, {"name", playlist.name}, {"items", items}});
    }
    return result;
}
// JSON numeric values are doubles even when the protocol expects IDs.
// Require exact integer equality and a positive representable int.
// Fractional values must not be truncated into another valid record ID.
// This validator is shared by queue, playlist, song and tag commands.
bool positiveId(const QJsonValue &value) {
    return value.isDouble() && value.toInt(-1) > 0 && value.toDouble() == value.toInt(-1);
}
Result<int> requiredId(const QJsonObject &params, const QString &key) {
    auto value = params.value(key);
    if (!positiveId(value))
        return failure(QStringLiteral("Invalid %1").arg(key));
    return value.toInt();
}
} // namespace nekotune
