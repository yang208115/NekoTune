#pragma once
#include "controllers/feature_controller.h"
#include "models/record_model.h"
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
        send("playlist." + action, data);
    }

  private:
    RecordModel m_model{"id"};
};
