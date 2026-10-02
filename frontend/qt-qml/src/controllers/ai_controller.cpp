#include "controllers/ai_controller.h"
#include <QPointer>
AiController::AiController(IpcClient &client) : FeatureController(client) {
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        if (event.value("event") == "server.connected")
            refreshConfiguration();
    });
}
void AiController::configRequest(const QString &method, const QJsonObject &params) {
    if (m_configBusy)
        return;
    m_configBusy = true;
    emit configBusyChanged();
    send(
        method, params,
        [this](const QJsonObject &data, const QString &) {
            m_configBusy = false;
            if (data.value("config").isObject()) {
                m_configuration = data.value("config").toObject().toVariantMap();
                emit configurationChanged();
            }
            emit configBusyChanged();
        },
        false);
}
void AiController::refreshConfiguration() { configRequest("ai.config.get"); }
void AiController::saveConfiguration(const QVariantMap &config) {
    configRequest("ai.config.set", QJsonObject::fromVariantMap(config));
}
void AiController::clearKey() { configRequest("ai.config.clear_key"); }
void AiController::testConnection() { configRequest("ai.test"); }
void AiController::discardSuggestion() {
    // Closing/changing editors invalidates the result locally; the remote request may still finish.
    ++m_generation;
    m_generating = false;
    m_suggestionError.clear();
    emit generatingChanged();
    emit suggestionErrorChanged();
}
void AiController::suggest(int songId, const QVariantMap &draft) {
    if (m_generating || songId <= 0)
        return;
    const auto generation = ++m_generation;
    m_generating = true;
    m_suggestionError.clear();
    emit generatingChanged();
    emit suggestionErrorChanged();
    QPointer<AiController> guard(this);
    m_client.request(
        "song.suggest_metadata", {{"song_id", songId}, {"draft", QJsonObject::fromVariantMap(draft)}},
        [guard, generation, songId](const QJsonObject &data, const QString &error) {
            if (!guard || generation != guard->m_generation)
                return;
            guard->m_generating = false;
            guard->m_suggestionError = error;
            emit guard->generatingChanged();
            emit guard->suggestionErrorChanged();
            if (error.isEmpty() && data.value("song_id").toInt() == songId)
                emit guard->suggestionReady(data.toVariantMap());
        },
        false);
}
