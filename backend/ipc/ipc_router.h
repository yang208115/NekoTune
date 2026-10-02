#pragma once
#include "application/command_scheduler.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <functional>
namespace nekotune {
class IpcRouter final : public QObject {
    Q_OBJECT
  public:
    using Completion = std::function<void(QJsonObject)>;
    using Handler = std::function<void(const QJsonObject &, Completion)>;
    /// serialized routes occupy the mutation queue until completion; queries and playback controls
    /// can remain immediate so slow imports do not block pause, seek or status requests.
    bool registerMethod(const QString &method, Handler handler, bool serialized = false);
    /// Validate method/params and correlate completion with the original JSON request ID.
    /// Handlers may finish immediately or later through their provided completion callback.
    /// Serialized handlers retain their command-queue slot until that completion occurs.
    /// Shutdown rejects new dispatch while admitted asynchronous handlers finish or cancel.
    void dispatch(const QJsonObject &request, Completion completion);
    void shutdown();
    CommandScheduler &commands() { return m_commands; }

  private:
    struct Route {
        Handler handler;
        bool serialized = false;
    };
    struct Command {
        QJsonObject params;
        Handler handler;
        Completion completion;
    };
    QHash<QString, Route> m_routes;
    CommandScheduler m_commands;
    bool m_stopping = false;
};
} // namespace nekotune
