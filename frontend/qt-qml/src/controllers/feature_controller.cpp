#include "controllers/feature_controller.h"
#include <QPointer>
void FeatureController::send(const QString &method, const QJsonObject &params,
                             IpcClient::Completion completion) {
    QPointer<FeatureController> guard(this);
    m_client.request(method, params,
                     [guard, method, completion](const QJsonObject &data, const QString &error) {
                         if (!guard)
                             return;
                         if (completion)
                             completion(data, error);
                         if (error.isEmpty())
                             emit guard->requestSucceeded(method);
                         else
                             emit guard->requestFailed(method, error);
                     });
}
