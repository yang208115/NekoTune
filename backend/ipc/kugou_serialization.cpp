#include "ipc/kugou_serialization.h"
#include <QJsonArray>
namespace nekotune {
QJsonObject toJson(const KugouStatus &status) {
    return {{"configured", status.configured},
            {"key_saved", status.keySaved},
            {"logged_in", status.loggedIn},
            {"busy", status.busy},
            {"download_active", status.downloadActive}};
}
QJsonObject toJson(const KugouEvent &event) {
    QJsonObject data;
    switch (event.type) {
    case KugouEventType::CodeSent:
        data.insert("event", "kugou.code_sent");
        break;
    case KugouEventType::LoggedIn:
        data.insert("event", "kugou.logged_in");
        break;
    case KugouEventType::OperationFailed:
        data.insert("event", "kugou.operation_failed");
        break;
    case KugouEventType::DownloadCancelled:
        data.insert("event", "kugou.download_cancelled");
        break;
    case KugouEventType::SearchResults: {
        data.insert("event", "kugou.search_results");
        data.insert("page", event.page);
        QJsonArray songs;
        for (const auto &song : event.songs)
            songs.append(QJsonObject{{"hash", song.hash},
                                     {"title", song.title},
                                     {"artist", song.artist},
                                     {"album", song.album},
                                     {"duration_ms", song.durationMs}});
        data.insert("songs", songs);
        break;
    }
    case KugouEventType::DownloadProgress:
        data = {{"event", "kugou.download_progress"}, {"received", event.received}, {"total", event.total}};
        break;
    case KugouEventType::DownloadStage:
        data = {{"event", "kugou.download_stage"}, {"stage", event.stage}};
        break;
    }
    if (!event.message.isEmpty())
        data.insert("message", event.message);
    return data;
}
} // namespace nekotune
