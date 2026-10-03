#include "controllers/lyrics_controller.h"
#include <QJsonArray>
LyricsController::LyricsController(IpcClient &client) : FeatureController(client) {
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        if (event.value("event") == "server.connected") {
            apply(event.value("data").toObject());
        }
        if (event.value("event") == "server.connected" || event.value("event") == "kugou.config_changed") {
            send("lyrics.sources", {}, [this](const QJsonObject &data, const QString &error) {
                if (error.isEmpty()) {
                    m_sources.clear();
                    for (const auto &source : data.value("sources").toArray())
                        if (source.toObject().value("supports_search").toBool())
                            m_sources.append(source.toObject().toVariantMap());
                    emit sourcesChanged();
                }
            });
        } else if (event.value("event") == "lyrics.changed")
            apply(event);
    });
    connect(&client, &IpcClient::responseReceived, this,
            [this](const QString &, const QJsonObject &data) { apply(data); });
}
void LyricsController::apply(const QJsonObject &data) {
    // Metadata responses contain a lyrics string, while playback snapshots contain an object.
    if (!data.value("lyrics").isObject())
        return;
    auto next = data.value("lyrics").toObject().toVariantMap();
    if (next == m_current)
        return;
    m_current = next;
    emit changed();
}
