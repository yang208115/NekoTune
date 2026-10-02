#include "ipc/api_context.h"
#include "app_paths.h"
namespace nekotune {
void registerLyricsApi(IpcRouter &router, ApiContext &api) {
    router.registerMethod("lyrics.sources", [&api](const auto &, auto done) {
        QJsonArray result;
        for (const auto &source : api.lyrics.sources())
            result.append(QJsonObject{
                {"id", source.id}, {"name", source.name}, {"supports_search", source.supportsSearch}});
        done(success({{"sources", result}}));
    });
    router.registerMethod("lyrics.refresh", [&api](const QJsonObject &params, auto done) {
        done(response(api.lyrics.refresh(params.value("track_id").toString())));
    });
    router.registerMethod("lyrics.set_offline", [&api](const QJsonObject &params, auto done) {
        if (!params.value("offline").isBool()) {
            done(error(failure("offline must be a boolean")));
            return;
        }
        if (!AppPaths::saveSetting("lyrics_offline", params.value("offline"))) {
            done(error(failure("Cannot save lyrics preference", ErrorCode::Storage)));
            return;
        }
        api.lyrics.setOffline(params.value("offline").toBool());
        done(success());
    });
    router.registerMethod("lyrics.search", [&api](const QJsonObject &params, auto done) {
        for (const QString field : {"title", "artist", "album"})
            if (params.contains(field) &&
                (!params.value(field).isString() || params.value(field).toString().size() > 500)) {
                done(error(failure("Search fields must be strings of at most 500 characters")));
                return;
            }
        if (params.contains("source") && !params.value("source").isString()) {
            done(error(failure("Invalid lyrics source")));
            return;
        }
        MetadataPatch patch;
        if (params.contains("title"))
            patch.title = params.value("title").toString();
        if (params.contains("artist"))
            patch.artist = params.value("artist").toString();
        done(response(
            api.lyrics.search(params.value("track_id").toString(), patch,
                              params.contains("album") ? params.value("album").toString() : QString(),
                              params.value("source").toString("lrclib"))));
    });
    router.registerMethod("lyrics.select", [&api](const QJsonObject &params, auto done) {
        auto value = params.value("index");
        if (!value.isDouble() || value.toInt(-1) < 0 || value.toDouble() != value.toInt(-1)) {
            done(error(failure("Invalid lyrics candidate index")));
            return;
        }
        done(response(api.lyrics.select(params.value("track_id").toString(),
                                        params.value("revision").toString().toULongLong(), value.toInt())));
    });
}
} // namespace nekotune
