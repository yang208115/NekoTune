#include "controllers/feature_controller.h"
#include <QPointer>
// Feature pages may disappear while backend work remains active.
// The guarded callback avoids updating a destroyed frontend owner.
// Run the feature completion before emitting its local outcome signal.
// This lets the feature settle busy/data state before UI reactions.
// Request IDs and reconnect behavior remain owned by IpcClient.
void FeatureController::send(const QString &method, const QJsonObject &params,
                             IpcClient::Completion completion, bool reportError) {
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
                     }, reportError);
}
