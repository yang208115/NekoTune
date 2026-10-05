#include "ipc/api/api_context.h"
#include <cmath>
#include <limits>
namespace nekotune {
// Navigation/queue mutations join the serialized command scheduler.
// Pause, stop, seek, volume and status remain directly responsive.
// Playing a path includes asynchronous inspection in that mutation slot.
// Playing without a path resumes the existing queue through PlayerEngine.
// Responses include authoritative state after the accepted operation.
// The client should reconcile that state instead of predicting selection.
void registerPlayerApi(IpcRouter &router, ApiContext &api) {
    router.registerMethod("player.audio_outputs", [&api](const auto &, auto done) {
        done(api.audioOutputs ? success(api.audioOutputStatus())
                              : error(failure("Audio outputs unavailable", ErrorCode::Unavailable)));
    });
    router.registerMethod("player.set_audio_output", [&api](const QJsonObject &params, auto done) {
        if (!api.audioOutputs) {
            done(error(failure("Audio outputs unavailable", ErrorCode::Unavailable)));
            return;
        }
        if (!params.value("device_id").isString()) {
            done(error(failure("device_id must be a string")));
            return;
        }
        if (params.contains("port_id") && !params.value("port_id").isString()) {
            done(error(failure("port_id must be a string")));
            return;
        }
        if (!params.value("port_id").toString().isEmpty()) {
            api.audioOutputs->selectPort(params.value("device_id").toString(), params.value("port_id").toString(),
                                         [&api, done](Result<void> result) { done(response(result, api.audioOutputStatus())); });
            return;
        }
        const auto result = api.audioOutputs->select(params.value("device_id").toString());
        done(response(result, api.audioOutputStatus()));
    });
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
        // Validate before converting floating point to signed milliseconds.
        // Infinity, negative values and out-of-range doubles must be rejected.
        // The domain seek operation then receives a representable nonnegative value.
        // Submillisecond input is intentionally truncated to the playback clock unit.
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
