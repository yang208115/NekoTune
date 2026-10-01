#pragma once
#include <QObject>
#include <QThread>
namespace nekotune {
class BackendSession;
class BackendRuntime final : public QObject {
    Q_OBJECT
  public:
    explicit BackendRuntime(QObject *parent = nullptr);
    ~BackendRuntime() override;
    bool start();
    void stop();
    QString errorString() const { return m_error; }
    QString serverName() const { return m_serverName; }

  private:
    QThread m_thread;
    QObject *m_context = nullptr;
    BackendSession *m_session = nullptr;
    QString m_error;
    QString m_serverName;
};
} // namespace nekotune
