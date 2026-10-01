#include "controllers/queue_controller.h"
#include <QJsonArray>
QueueController::QueueController(IpcClient &client) : FeatureController(client) {
    auto apply = [this](const QJsonObject &data) {
        if (data.contains("queue"))
            m_model.update(data.value("queue").toArray().toVariantList());
    };
    connect(&client, &IpcClient::eventReceived, this, [apply](const QJsonObject &event) {
        apply(event.value("event") == "server.connected" ? event.value("data").toObject() : event);
    });
    connect(&client, &IpcClient::responseReceived, this,
            [apply](const QString &, const QJsonObject &data) { apply(data); });
}
