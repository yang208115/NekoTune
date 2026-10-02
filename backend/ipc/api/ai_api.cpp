#include "ipc/api/api_context.h"
namespace nekotune {
namespace {
// Build the public configuration shape explicitly instead of serializing
// the credential store or an internal request object.
// key_saved and credential_error are sufficient for settings feedback.
// Neither configuration reads nor save replies can return API key bytes.
QJsonObject configJson(const AiConfig &config) {
    return {{"base_url", config.baseUrl},
            {"model", config.model},
            {"configured", config.configured()},
            {"key_saved", config.keySaved},
            {"credential_error", config.credentialError}};
}
// The preview endpoint accepts draft values without writing the library.
// Validate every present field before dispatching model work.
// Keep fields optional so absence can use the stored song snapshot.
// Input bounds prevent oversized IPC drafts from expanding worker state.
// The generation adapter applies its narrower prompt/output limits later.
Result<MetadataPatch> readDraft(const QJsonValue &value) {
    if (!value.isUndefined() && !value.isObject())
        return failure("draft must be an object");
    const auto object = value.toObject();
    MetadataPatch draft;
    for (const auto &[name, field] : QList<QPair<QString, std::optional<QString> *>>{
             {"custom_title", &draft.title}, {"artist", &draft.artist}, {"lyrics", &draft.lyrics}}) {
        if (!object.contains(name))
            continue;
        if (!object.value(name).isString() || object.value(name).toString().size() > 2 * 1024 * 1024)
            return failure(name + " must be a bounded string");
        *field = object.value(name).toString();
    }
    if (object.contains("tags")) {
        if (!object.value("tags").isArray() || object.value("tags").toArray().size() > 200)
            return failure("tags must be an array of at most 200 names");
        QStringList tags;
        for (const auto &value : object.value("tags").toArray()) {
            if (!value.isString() || value.toString().trimmed().isEmpty() || value.toString().size() > 64)
                return failure("Each tag must contain 1 to 64 characters");
            tags.append(value.toString().trimmed());
        }
        draft.tags = tags;
    }
    return draft;
}
} // namespace
void registerAiApi(IpcRouter &router, ApiContext &api) {
    auto configDone = [](auto done) {
        return [done](Result<AiConfig> result) {
            done(result ? success({{"config", configJson(result.value())}}) : error(result.error()));
        };
    };
    router.registerMethod("ai.config.get", [&api, configDone](const auto &, auto done) {
        api.ai->configuration(configDone(done));
    });
    router.registerMethod("ai.config.set", [&api, configDone](const QJsonObject &params, auto done) {
        if (!params.value("base_url").isString() || !params.value("model").isString() ||
            (params.contains("api_key") && !params.value("api_key").isString())) {
            done(error(failure("base_url, model and optional api_key must be strings")));
            return;
        }
        AiConfigUpdate update{params.value("base_url").toString(), params.value("model").toString()};
        if (params.contains("api_key"))
            update.apiKey = params.value("api_key").toString();
        api.ai->configure(update, configDone(done));
    });
    router.registerMethod("ai.config.clear_key", [&api, configDone](const auto &, auto done) {
        api.ai->clearKey(configDone(done));
    });
    router.registerMethod("ai.test", [&api](const auto &, auto done) {
        api.ai->test([done](Result<AiSuggestion> result) {
            done(result ? success({{"connected", true}}) : error(result.error()));
        });
    });
    // A potentially slow network call must never hold CommandScheduler's mutation queue.
    router.registerMethod("song.suggest_metadata", [&api](const QJsonObject &params, auto done) {
        const auto id = requiredId(params, "song_id");
        const auto draft = readDraft(params.value("draft"));
        if (!id || !draft) {
            done(error(!id ? id.error() : draft.error()));
            return;
        }
        api.ai->suggest(id.value(), draft.value(), [done, songId = id.value()](Result<AiSuggestion> result) {
            if (!result) {
                done(error(result.error()));
                return;
            }
            const auto &suggestion = result.value();
            done(success({{"song_id", songId},
                          {"custom_title", suggestion.title},
                          {"artist", suggestion.artist},
                          {"tags", QJsonArray::fromStringList(suggestion.tags)},
                          {"warning", suggestion.warning}}));
        });
    });
}
} // namespace nekotune
