#include "ipc/api/api_context.h"
#include <cmath>
#include <limits>
namespace nekotune {
void registerPlayerApi(IpcRouter &router, ApiContext &api) {
    auto control = [&](const QString &name, auto operation, bool serialized) {
        router.registerMethod(
            name,
            [&api, operation](const QJsonObject &, auto done) {
                auto result = (api.player.*operation)();
                done(response(result, api.status()));
            },
            serialized);
    };
    control("player.toggle_play_pause", &PlayerEngine::toggle, true);
    control("player.pause", &PlayerEngine::pause, false);
    control("player.stop", &PlayerEngine::stop, false);
    control("player.next", &PlayerEngine::next, true);
    control("player.previous", &PlayerEngine::previous, true);
    router.registerMethod("player.set_playback_mode", [&api](const QJsonObject &params, auto done) {
        const auto value = params.value("mode");
        const auto mode = value.isString() ? playbackModeFromString(value.toString()) : std::nullopt;
        if (!mode) {
            done(error(failure("mode must be sequential, repeat_one, shuffle, or repeat_all")));
            return;
        }
        const auto result = api.player.setPlaybackMode(*mode);
        done(response(result, api.status()));
    }, true);
    router.registerMethod("player.status", [&api](const auto &, auto done) { done(success(api.status())); });
    router.registerMethod("queue.status",
                          [&api](const auto &, auto done) { done(success(api.queueStatus())); });
    router.registerMethod("player.seek", [&api](const QJsonObject &params, auto done) {
        auto value = params.value("position");
        double number = value.toDouble(-1);
        if (!value.isDouble() || !std::isfinite(number) || number < 0 ||
            number >= double(std::numeric_limits<qint64>::max())) {
            done(error(failure("position must be a nonnegative finite number")));
            return;
        }
        auto result = api.player.seek(qint64(number));
        done(response(result, api.status()));
    });
    router.registerMethod("player.set_volume", [&api](const QJsonObject &params, auto done) {
        if (!params.value("volume").isDouble()) {
            done(error(failure("volume must be a number")));
            return;
        }
        auto result = api.player.setVolume(params.value("volume").toDouble());
        done(response(result, api.status()));
    });
    router.registerMethod(
        "queue.clear",
        [&api](const auto &, auto done) {
            auto result = api.player.clear();
            done(response(result, api.queueStatus()));
        },
        true);
    for (bool play : {true, false})
        router.registerMethod(
            play ? "queue.play" : "queue.remove",
            [&api, play](const QJsonObject &params, auto done) {
                auto id = requiredId(params, "id");
                if (!id) {
                    done(error(id.error()));
                    return;
                }
                auto result = play ? api.player.playItem(id.value()) : api.player.removeItem(id.value());
                done(response(result, play ? api.status() : api.queueStatus()));
            },
            true);
    for (bool play : {true, false})
        router.registerMethod(
            play ? "player.play" : "queue.add",
            [&api, play](const QJsonObject &params, auto done) {
                const auto path = params.value("path");
                if (play && (path.isUndefined() || (path.isString() && path.toString().isEmpty()))) {
                    auto result = api.player.play();
                    done(response(result, api.status()));
                    return;
                }
                if (!path.isString() || path.toString().isEmpty()) {
                    done(error(failure("path is required")));
                    return;
                }
                api.imports.inspect(path.toString(), [&api, play, done](Result<ImportedFile> file) {
                    if (!file) {
                        done(error(file.error()));
                        return;
                    }
                    auto result = api.collections.enqueue(file.value(), play);
                    done(result ? success(play ? api.status() : api.queueStatus()) : error(result.error()));
                });
            },
            true);
}
} // namespace nekotune
