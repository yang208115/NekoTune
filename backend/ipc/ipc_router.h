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
    bool registerMethod(const QString &method, Handler handler, bool serialized = false);
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
