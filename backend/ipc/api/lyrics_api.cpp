#include "ipc/api/api_context.h"
#include "app_paths.h"
namespace nekotune {
// Lyrics commands acknowledge admission rather than waiting for network work.
// The changed event later supplies the new revision's document/candidates.
// Track identity guards refresh and search against stale UI selections.
// Candidate selection includes revision as well as candidate index.
// Offline preference is persisted before updating the running controller.
void registerLyricsApi(IpcRouter &router, ApiContext &api) {
    router.registerMethod("lyrics.sources", [&api](const auto &, auto done) {
        QJsonArray result;
        for (const auto &source : api.lyrics.sources()) {
            if (source.id == "kugou" && !api.kugou.status().enabled)
                continue;
            result.append(QJsonObject{
                {"id", source.id}, {"name", source.name}, {"supports_search", source.supportsSearch}});
        }
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
    // Candidate indices must be integral and nonnegative at the wire boundary.
    // Revision is encoded as decimal text to preserve full 64-bit identity.
    // The controller additionally checks track/revision and vector bounds.
    // This protects old result-list clicks without exposing provider credentials.
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
