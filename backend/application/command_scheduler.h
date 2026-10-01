#pragma once
#include <QObject>
#include <deque>
#include <functional>
namespace nekotune {
class CommandScheduler final : public QObject {
    Q_OBJECT
  public:
    using Done = std::function<void()>;
    using Task = std::function<void(Done)>;
    void submit(Task task, Done cancelled = {});
    void shutdown();

  private:
    void advance();
    struct Command {
        Task task;
        Done cancelled;
    };
    std::deque<Command> m_commands;
    bool m_busy = false;
    bool m_stopping = false;
};
} // namespace nekotune
