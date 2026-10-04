#include "application/extensions/extension_service.h"
#include "ipc/api/api_context.h"
namespace nekotune {
void registerExtensionsApi(IpcRouter &router, ApiContext &api) {
    for (const QString method :
         {"extensions.list", "extensions.install", "extensions.enable", "extensions.disable",
          "extensions.reload", "extensions.uninstall", "extensions.get_config", "extensions.set_config",
          "extensions.logs", "extensions.call", "extensions.select"}) {
        router.registerMethod(method, [&api, method](const QJsonObject &params, auto done) {
            if (!api.extensions) {
                done(error(failure("Extensions unavailable")));
                return;
            }
            if (method == "extensions.list") {
                done(success(QJsonObject::fromVariantMap(api.extensions->snapshot())));
                return;
            }
            api.extensions->request(method, params.toVariantMap(), [done](Result<QVariantMap> result) {
                done(result ? success(QJsonObject::fromVariantMap(result.value())) : error(result.error()));
            });
        });
    }
}
} // namespace nekotune
