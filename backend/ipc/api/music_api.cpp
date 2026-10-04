#include "application/extensions/music_service.h"
#include "ipc/api/api_context.h"
namespace nekotune {
namespace {
Result<SongMetadata> remoteSong(const QJsonObject &params) {
    const auto track = params.value("track").toObject();
    SongMetadata song;
    song.providerId = params.value("source").toString();
    song.providerTrackId = track.value("id").toString();
    song.customTitle = track.value("title").toString();
    song.artist = track.value("artist").toString();
    song.album = track.value("album").toString();
    song.coverUrl = track.value("cover_url").toString();
    song.durationMs = track.value("duration_ms").toVariant().toLongLong();
    if (song.providerId.isEmpty() || song.providerTrackId.isEmpty() || song.customTitle.isEmpty() ||
        song.durationMs < 0)
        return failure("A source and track with id/title are required");
    return song;
}
} // namespace
void registerMusicApi(IpcRouter &router, ApiContext &api) {
    router.registerMethod("music.cancel", [&api](const QJsonObject &params, auto done) {
        api.music->cancelDownload(params.value("task").toString(), [done](Result<QVariantMap> result) {
            done(result ? success() : error(result.error()));
        });
    });
    router.registerMethod("music.sources", [&api](const QJsonObject &, auto done) {
        done(success({{"sources", QJsonArray::fromVariantList(api.music->sources())}}));
    });
    router.registerMethod("music.track", [&api](const QJsonObject &params, auto done) {
        api.music->track(params.value("source").toString(), params.value("id").toString(),
                         [done](Result<QVariantMap> result) {
                             done(result ? success(QJsonObject::fromVariantMap(result.value()))
                                         : error(result.error()));
                         });
    });
    router.registerMethod("music.search", [&api](const QJsonObject &params, auto done) {
        if (!api.music->available(params.value("source").toString())) {
            done(error(failure("Music source unavailable")));
            return;
        }
        api.music->search(
            params.value("source").toString(), params.toVariantMap(), [done](Result<QVariantMap> result) {
                done(result ? success(QJsonObject::fromVariantMap(result.value())) : error(result.error()));
            });
    });
    for (const QString method : {"music.enqueue", "music.add_to_playlist", "music.download"}) {
        router.registerMethod(
            method,
            [&api, method](const QJsonObject &params, auto done) {
                auto song = remoteSong(params);
                if (!song) {
                    done(error(song.error()));
                    return;
                }
                if (!api.music->available(song.value().providerId)) {
                    done(error(failure("Music source unavailable")));
                    return;
                }
                if (method == "music.enqueue") {
                    auto result = api.collections.enqueueRemote(song.value(), params.value("play").toBool());
                    done(result ? success({{"queue_id", result.value()}}) : error(result.error()));
                } else if (method == "music.add_to_playlist") {
                    auto id = requiredId(params, "playlist_id");
                    if (!id) {
                        done(error(id.error()));
                        return;
                    }
                    done(response(api.collections.addRemoteToPlaylist(id.value(), song.value())));
                } else
                    done(success({{"task", api.music->download(song.value().providerId, song.value())}}));
            },
            method != "music.download");
    }
}
} // namespace nekotune
