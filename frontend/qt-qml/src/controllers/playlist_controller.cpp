#include "controllers/playlist_controller.h"
#include <QJsonArray>
PlaylistController::PlaylistController(IpcClient &client) : FeatureController(client) {
    auto apply = [this](const QJsonObject &data) {
        if (data.contains("playlists"))
            m_model.update(data.value("playlists").toArray().toVariantList());
    };
    connect(&client, &IpcClient::eventReceived, this, [apply](const QJsonObject &event) {
        apply(event.value("event") == "server.connected" ? event.value("data").toObject() : event);
    });
    connect(&client, &IpcClient::responseReceived, this,
            [apply](const QString &, const QJsonObject &data) { apply(data); });
}
