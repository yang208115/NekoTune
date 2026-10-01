#include "ipc/ipc_router.h"
#include "ipc/serialization.h"
#include <QPointer>
#include <QTimer>
#include <memory>
namespace nekotune {
bool IpcRouter::registerMethod(const QString &method, Handler handler, bool serialized) {
    if (method.isEmpty() || m_routes.contains(method))
        return false;
    m_routes.insert(method, {std::move(handler), serialized});
    return true;
}
void IpcRouter::dispatch(const QJsonObject &request, Completion completion) {
    auto id = request.value("id");
    auto once = std::make_shared<bool>(false);
    auto done = [completion = std::move(completion), id, once](QJsonObject result) {
        if (*once)
            return;
        *once = true;
        if (!id.isUndefined())
            result.insert("id", id);
        completion(result);
    };
    const auto method = request.value("method").toString();
    if (m_stopping) {
        done(error(failure(QStringLiteral("Backend is shutting down"), ErrorCode::Cancelled)));
        return;
    }
    if (method.isEmpty()) {
        done(error(failure(QStringLiteral("Request method is required"))));
        return;
    }
    if (!m_routes.contains(method)) {
        done(error(failure(QStringLiteral("Unknown method: %1").arg(method))));
        return;
    }
    const auto params = request.value("params");
    if (!params.isUndefined() && !params.isObject()) {
        done(error(failure(QStringLiteral("params must be an object"))));
        return;
    }
    auto route = m_routes.value(method);
    if (!route.serialized) {
        route.handler(params.toObject(), done);
        return;
    }
    m_commands.submit(
        [route, params = params.toObject(), done](CommandScheduler::Done next) {
            route.handler(params, [done, next](QJsonObject result) {
                done(result);
                next();
            });
        },
        [done] { done(error(failure(QStringLiteral("Backend is shutting down"), ErrorCode::Cancelled))); });
}
void IpcRouter::shutdown() {
    m_stopping = true;
    m_commands.shutdown();
}
} // namespace nekotune
