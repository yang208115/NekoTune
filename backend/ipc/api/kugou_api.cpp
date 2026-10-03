#include "ipc/api/api_context.h"
namespace nekotune {
// Provider start operations return their immediate admission outcome.
// Completion/progress is delivered by separate kugou events.
// Status serialization contains account flags rather than secret values.
// Account keys enter only the explicit save operation's input field.
// Cookies remain inside the provider/session adapters throughout.
void registerKugouApi(IpcRouter &router, ApiContext &api) {
    router.registerMethod("kugou.status", [&api](const auto &, auto done) {
        done(success({{"kugou", toJson(api.kugou.status())}}));
    });
    const QHash<QString, std::function<Result<void>(const QJsonObject &)>> methods{
        {"config.set",
         [&api](const auto &p) {
             if (!p.value("enabled").isBool() || !p.value("worker_url").isString())
                 return Result<void>{failure(QStringLiteral("enabled must be a boolean and worker_url a string"))};
             return api.kugou.saveConfiguration(p.value("enabled").toBool(), p.value("worker_url").toString());
         }},
        {"send_code",
         [&api](const auto &p) { return api.kugou.startCodeRequest(p.value("mobile").toString()); }},
        {"login",
         [&api](const auto &p) {
             return api.kugou.startLogin(p.value("mobile").toString(), p.value("code").toString());
         }},
        {"search",
         [&api](const auto &p) {
             return api.kugou.startSearch(p.value("keywords").toString(), p.value("page").toInt(1));
         }},
        {"download", [&api](const auto &p) { return api.kugou.startDownload(p.value("hash").toString()); }},
        {"cancel", [&api](const auto &) { return api.kugou.cancelDownload(); }},
        {"save_key",
         [&api](const auto &p) {
             return p.value("key").isString() ? api.kugou.saveAccountKey(p.value("key").toString())
                                              : Result<void>{failure(QStringLiteral("key must be a string"))};
         }},
        {"clear_key", [&api](const auto &) { return api.kugou.clearAccountKey(); }}};
    for (auto it = methods.cbegin(); it != methods.cend(); ++it)
        router.registerMethod(
            "kugou." + it.key(), [&api, operation = it.value()](const QJsonObject &params, auto done) {
                auto result = operation(params);
                done(result ? success({{"kugou", toJson(api.kugou.status())}}) : error(result.error()));
            });
}
} // namespace nekotune
