#include "runtime/backend_runtime.h"
#include "runtime/backend_session.h"
namespace nekotune {
BackendRuntime::BackendRuntime(QObject *parent) : QObject(parent) {}
BackendRuntime::~BackendRuntime() { stop(); }
bool BackendRuntime::start() {
    if (m_thread.isRunning())
        return m_session != nullptr;
    m_context = new QObject;
    m_context->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_context, &QObject::deleteLater);
    m_thread.start();
    bool ok = false;
    // Construct the session here so SQL, socket and network objects acquire backend-thread affinity.
    // Blocking also keeps the captured startup result alive until initialization finishes.
    QMetaObject::invokeMethod(
        m_context,
        [this, &ok] {
            m_session = new BackendSession;
            ok = m_session->start();
            m_error = m_session->errorString();
            m_serverName = m_session->serverName();
            if (!ok) {
                delete m_session;
                m_session = nullptr;
            }
        },
        Qt::BlockingQueuedConnection);
    if (!ok)
        stop();
    return ok;
}
void BackendRuntime::stop() {
    if (!m_thread.isRunning())
        return;
    // Destroy services while their event loop still runs, so shutdown can complete worker callbacks.
    QMetaObject::invokeMethod(
        m_context,
        [this] {
            delete m_session;
            m_session = nullptr;
        },
        Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    m_context = nullptr;
}
} // namespace nekotune
