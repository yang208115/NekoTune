#pragma once
#include "controllers/feature_controller.h"
#include "models/record_model.h"
class QueueController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(RecordModel *model READ model CONSTANT)
  public:
    explicit QueueController(IpcClient &client);
    RecordModel *model() { return &m_model; }
    Q_INVOKABLE void addPath(const QString &path) {
        send("queue.add", {{"path", IpcClient::normalizePath(path)}});
    }
    Q_INVOKABLE void playQueueItem(int id) { send("queue.play", {{"id", id}}); }
    Q_INVOKABLE void removeQueueItem(int id) { send("queue.remove", {{"id", id}}); }
    Q_INVOKABLE void clearQueue() { send("queue.clear"); }

  private:
    RecordModel m_model{"queue_id"};
};
