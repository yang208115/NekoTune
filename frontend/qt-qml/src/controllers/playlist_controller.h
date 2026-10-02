#pragma once
#include "controllers/feature_controller.h"
#include "models/record_model.h"
#include <QJsonArray>
/// Owns playlist collection snapshots independently of queue state.
/// Management requests use playlist ID plus song/queue/path parameters.
/// Playing a playlist is an explicit backend queue-replacement operation.
/// Ordinary membership edits and list refreshes do not start playback.
/// The created signal lets the shell navigate to a confirmed new collection.
class PlaylistController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(RecordModel *model READ model CONSTANT)
  public:
    explicit PlaylistController(IpcClient &client);
    RecordModel *model() { return &m_model; }
    Q_INVOKABLE void managePlaylist(const QString &action, const QVariantMap &params) {
        auto data = QJsonObject::fromVariantMap(params);
        if (data.contains("path"))
            data.insert("path", IpcClient::normalizePath(data.value("path").toString()));
        send("playlist." + action, data, [this, action](const QJsonObject &result, const QString &error) {
            if (action == "create" && error.isEmpty()) {
                m_model.update(result.value("playlists").toArray().toVariantList());
                emit playlistCreated(result.value("playlist_id").toInt());
            }
        });
    }

  signals:
    void playlistCreated(int id);

  private:
    RecordModel m_model{"id"};
};
