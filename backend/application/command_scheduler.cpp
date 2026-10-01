#include "application/command_scheduler.h"
#include <QPointer>
#include <QTimer>
#include <memory>
namespace nekotune {
void CommandScheduler::submit(Task task, Done cancelled) {
    if (m_stopping) {
        if (cancelled)
            cancelled();
        return;
    }
    m_commands.push_back({std::move(task), std::move(cancelled)});
    advance();
}
void CommandScheduler::advance() {
    if (m_stopping || m_busy || m_commands.empty())
        return;
    m_busy = true;
    auto command = std::move(m_commands.front());
    m_commands.pop_front();
    QPointer<CommandScheduler> guard(this);
    auto once = std::make_shared<bool>(false);
    command.task([guard, once] {
        if (*once)
            return;
        *once = true;
        if (!guard)
            return;
        guard->m_busy = false;
        QTimer::singleShot(0, guard, [guard] {
            if (guard)
                guard->advance();
        });
    });
}
void CommandScheduler::shutdown() {
    m_stopping = true;
    for (auto &command : m_commands)
        if (command.cancelled)
            command.cancelled();
    m_commands.clear();
}
} // namespace nekotune
