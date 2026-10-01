#include "controllers/kugou_controller.h"
KugouController::KugouController(IpcClient &client) : FeatureController(client) {
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        auto name = event.value("event").toString();
        if (name == "server.connected")
            apply(event.value("data").toObject());
        else if (name.startsWith("kugou.")) {
            emit kugouEvent(event.toVariantMap());
            if (name != "kugou.download_progress" && name != "kugou.download_stage")
                send("kugou.status");
        }
    });
    connect(&client, &IpcClient::responseReceived, this,
            [this](const QString &, const QJsonObject &data) { apply(data); });
}
void KugouController::apply(const QJsonObject &data) {
    if (!data.contains("kugou"))
        return;
    auto next = data.value("kugou").toObject().toVariantMap();
    if (next == m_account)
        return;
    m_account = next;
    emit changed();
}
