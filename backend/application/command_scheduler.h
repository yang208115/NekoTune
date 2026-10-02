#pragma once
#include <QObject>
#include <deque>
#include <functional>
namespace nekotune {
/// Serializes mutations on its owning event-loop thread, including time spent awaiting callbacks.
class CommandScheduler final : public QObject {
    Q_OBJECT
  public:
    using Done = std::function<void()>;
    using Task = std::function<void(Done)>;
    /// A started task must call Done on this thread on success, failure or cancellation.
    /// The cancelled callback applies only to tasks that have not started.
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
