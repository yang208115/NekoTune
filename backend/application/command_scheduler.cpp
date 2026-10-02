#include "application/command_scheduler.h"
#include <QPointer>
#include <QTimer>
#include <memory>
namespace nekotune {
// Admission and execution happen on the same event-loop thread.
// The queue protects logical mutations across asynchronous waits.
// It does not acquire a database transaction for the task itself.
// That transaction starts when the task has its validated inputs.
// Tasks submitted during shutdown are cancelled without execution.
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
    // Completion paths can converge; advancing twice would overlap queued mutations.
    auto once = std::make_shared<bool>(false);
    command.task([guard, once] {
        if (*once)
            return;
        *once = true;
        if (!guard)
            return;
        guard->m_busy = false;
        // Yield even for synchronous tasks so a long queue cannot recurse through advance().
        QTimer::singleShot(0, guard, [guard] {
            if (guard)
                guard->advance();
        });
    });
}
// Pending commands have not yet acquired their execution slot.
// Run their cancellation replies before dropping their closures.
// The already-started task owns its asynchronous completion path.
// Its executor is shut down separately by BackendSession.
// The stopping flag prevents completion from admitting further work.
void CommandScheduler::shutdown() {
    m_stopping = true;
    for (auto &command : m_commands)
        if (command.cancelled)
            command.cancelled();
    m_commands.clear();
}
} // namespace nekotune
