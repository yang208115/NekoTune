#include "ipc/api/api_context.h"
namespace nekotune {
namespace {
Result<std::optional<QVector<int>>> requestedSongs(const QJsonObject &params) {
    if (!params.contains("song_ids"))
        return std::optional<QVector<int>>{};
    const auto values = params.value("song_ids");
    if (!values.isArray() || values.toArray().isEmpty())
        return failure("song_ids must be a non-empty array");
    QVector<int> ids;
    for (const auto &value : values.toArray()) {
        if (!positiveId(value) || ids.contains(value.toInt()))
            return failure("song_ids must contain unique positive ids");
        ids.append(value.toInt());
    }
    return std::optional<QVector<int>>{ids};
}
}
void registerLibraryApi(IpcRouter &router, ApiContext &api) {
    router.registerMethod("library.scan", [&api](const auto &, auto done) {
        done(success({{"started", api.scan && api.scan()}}));
    });
    router.registerMethod("library.list", [&api](const auto &, auto done) {
        done(success({{"library", api.libraryStatus()}}));
    });
    router.registerMethod(
        "library.import",
        [&api](const QJsonObject &params, auto done) {
            auto path = params.value("path");
            if (!path.isString() || path.toString().isEmpty()) {
                done(error(failure("path is required")));
                return;
            }
            api.imports.inspect(path.toString(), [&api, done](Result<ImportedFile> file) {
                if (!file) {
                    done(error(file.error()));
                    return;
                }
                auto result = api.library.importFile(file.value());
                done(result ? success({{"song_id", result.value().id}, {"path", file.value().path}})
                            : error(result.error()));
            });
        },
        true);
    router.registerMethod("song.metadata", [&api](const QJsonObject &params, auto done) {
        auto id = requiredId(params, params.contains("song_id") ? "song_id" : "id");
        if (!id) {
            done(error(id.error()));
            return;
        }
        auto result = api.library.metadata(id.value());
        if (!result) {
            done(error(result.error()));
            return;
        }
        auto data = toJson(result.value());
        data.insert("tags", toJson(api.library.tagsFor(id.value())));
        done(success(data));
    });
    router.registerMethod(
        "song.update_metadata",
        [&api](const QJsonObject &params, auto done) {
            auto id = requiredId(params, params.contains("song_id") ? "song_id" : "id");
            if (!id) {
                done(error(id.error()));
                return;
            }
            MetadataPatch patch;
            const QString titleKey = params.contains("custom_title") ? "custom_title" : "title",
                          artistKey = params.contains("artist") ? "artist" : "author";
            for (const auto &key : {titleKey, artistKey, QString("lyrics")})
                if (params.contains(key) && !params.value(key).isString()) {
                    done(error(failure(key + " must be a string")));
                    return;
                }
            if (params.contains(titleKey))
                patch.title = params.value(titleKey).toString();
            if (params.contains(artistKey))
                patch.artist = params.value(artistKey).toString();
            if (params.contains("lyrics"))
                patch.lyrics = params.value("lyrics").toString();
            if (params.contains("tags")) {
                if (!params.value("tags").isArray()) {
                    done(error(failure("tags must be an array")));
                    return;
                }
                QStringList names;
                for (const auto &tag : params.value("tags").toArray()) {
                    if (!tag.isString()) {
                        done(error(failure("Each tag must be a string")));
                        return;
                    }
                    names.append(tag.toString());
                }
                patch.tags = names;
            }
            auto result = api.library.update(id.value(), patch);
            if (!result) {
                done(error(result.error()));
                return;
            }
            auto data = toJson(result.value());
            data.insert("tags", toJson(api.library.tagsFor(id.value())));
            done(success(data));
        },
        true);
    router.registerMethod(
        "library.delete",
        [&api](const QJsonObject &params, auto done) {
            if (params.contains("clean_files") && !params.value("clean_files").isBool()) {
                done(error(failure("clean_files must be a boolean")));
                return;
            }
            const bool cleanFiles = params.value("clean_files").toBool();
            if (cleanFiles && api.kugou.status().downloadActive) {
                done(error(failure("Wait for the active music download before cleaning files")));
                return;
            }
            auto values = params.value("song_ids");
            if (!values.isArray() || values.toArray().isEmpty()) {
                done(error(failure("song_ids must be a non-empty array")));
                return;
            }
            QVector<int> ids;
            for (const auto &value : values.toArray()) {
                if (!positiveId(value)) {
                    done(error(failure("Invalid song id")));
                    return;
                }
                ids.append(value.toInt());
            }
            auto result = api.collections.deleteSongs(ids, cleanFiles);
            done(result ? success({{"deleted_count", result.value().deletedCount},
                                   {"cleanup_errors", QJsonArray::fromStringList(result.value().cleanupErrors)}})
                        : error(result.error()));
        },
        true);
    router.registerMethod(
        "library.play",
        [&api](const QJsonObject &params, auto done) {
            auto values = params.value("tag_ids");
            if (!values.isUndefined() && !values.isArray()) {
                done(error(failure("tag_ids must be an array")));
                return;
            }
            QVector<int> ids;
            for (const auto &value : values.toArray()) {
                if (!positiveId(value)) {
                    done(error(failure("Invalid tag id")));
                    return;
                }
                ids.append(value.toInt());
            }
            if (params.contains("song_id") && !positiveId(params.value("song_id"))) {
                done(error(failure("Invalid song id")));
                return;
            }
            auto selected = requestedSongs(params);
            if (!selected) {
                done(error(selected.error()));
                return;
            }
            auto result = api.collections.playLibrary(ids, params.value("song_id").toInt(), selected.value());
            if (!result) {
                done(error(result.error()));
                return;
            }
            auto data = api.status();
            QJsonArray skipped;
            for (int id : result.value().skippedSongIds)
                skipped.append(id);
            data.insert("skipped_song_ids", skipped);
            done(success(data));
        },
        true);
    router.registerMethod("playlist.list", [&api](const auto &, auto done) {
        done(success({{"playlists", api.playlistList()}}));
    });
    router.registerMethod(
        "playlist.create",
        [&api](const QJsonObject &params, auto done) {
            auto result = api.playlists.create(params.value("name").toString());
            done(result ? success({{"playlist_id", result.value()}, {"playlists", api.playlistList()}})
                        : error(result.error()));
        },
        true);
    for (const QString action : {"rename", "delete", "remove", "play", "add"})
        router.registerMethod(
            "playlist." + action,
            [&api, action](const QJsonObject &params, auto done) {
                auto id = requiredId(params, "id");
                if (!id) {
                    done(error(id.error()));
                    return;
                }
                auto finish = [&api, done, action](Result<void> result) {
                    done(response(result, action == "play" ? api.status()
                                                           : QJsonObject{{"playlists", api.playlistList()}}));
                };
                if (action == "rename") {
                    finish(api.playlists.rename(id.value(), params.value("name").toString()));
                    return;
                }
                if (action == "delete") {
                    finish(api.playlists.remove(id.value()));
                    return;
                }
                if (action == "play") {
                    if (params.contains("song_id") && !positiveId(params.value("song_id"))) {
                        done(error(failure("Invalid song id")));
                        return;
                    }
                    auto selected = requestedSongs(params);
                    if (!selected) {
                        done(error(selected.error()));
                        return;
                    }
                    finish(api.collections.playPlaylist(id.value(), params.value("song_id").toInt(), selected.value()));
                    return;
                }
                if (action == "remove" || params.contains("song_id")) {
                    auto songId = requiredId(params, "song_id");
                    if (!songId) {
                        done(error(songId.error()));
                        return;
                    }
                    finish(action == "remove"
                               ? api.playlists.removeSong(id.value(), songId.value())
                               : api.collections.addSongToPlaylist(id.value(), songId.value()));
                    return;
                }
                if (params.contains("queue_id")) {
                    auto queueId = requiredId(params, "queue_id");
                    if (!queueId) {
                        done(error(queueId.error()));
                        return;
                    }
                    finish(api.collections.addQueueItemToPlaylist(id.value(), queueId.value()));
                    return;
                }
                auto path = params.value("path");
                if (!path.isString() || path.toString().isEmpty()) {
                    done(error(failure("path is required")));
                    return;
                }
                api.imports.inspect(path.toString(),
                                    [&api, id = id.value(), finish](Result<ImportedFile> file) {
                                        if (!file) {
                                            finish(file.error());
                                            return;
                                        }
                                        finish(api.collections.addFileToPlaylist(id, file.value()));
                                    });
            },
            true);
    for (const QString action : {"create", "rename", "delete"})
        router.registerMethod(
            "tag." + action,
            [&api, action](const QJsonObject &params, auto done) {
                if (action != "delete" && !params.value("name").isString()) {
                    done(error(failure("Tag name is required")));
                    return;
                }
                if (action == "create") {
                    auto result = api.tags.create(params.value("name").toString());
                    done(result ? success({{"tag_id", result.value()}}) : error(result.error()));
                    return;
                }
                auto id = requiredId(params, "id");
                if (!id) {
                    done(error(id.error()));
                    return;
                }
                done(response(action == "rename"
                                  ? api.tags.rename(id.value(), params.value("name").toString())
                                  : api.tags.remove(id.value())));
            },
            true);
}
} // namespace nekotune
