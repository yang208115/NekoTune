#include "ipc/api_context.h"
#include <QFileInfo>
namespace nekotune {
QJsonObject ApiContext::status() const {
    auto state = toJson(player.snapshot());
    state.insert("queue", queueJson(queue.queue()));
    state.insert("playlists", playlistList());
    state.insert("database_path", databasePath);
    state.insert("lyrics", toJson(lyrics.snapshot()));
    state.insert("kugou", toJson(kugou.status()));
    return state;
}
QJsonObject ApiContext::queueStatus() const {
    return {{"current_index", queue.queue().currentIndex()}, {"items", queueJson(queue.queue())}};
}
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
            item.insert("title", song.value().customTitle.trimmed().isEmpty()
                                     ? QFileInfo(record.path).completeBaseName()
                                     : song.value().customTitle.trimmed());
            items.append(item);
        }
        result.append(QJsonObject{{"id", playlist.id}, {"name", playlist.name}, {"items", items}});
    }
    return result;
}
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
