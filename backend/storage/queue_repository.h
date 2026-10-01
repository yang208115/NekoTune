#pragma once
#include "domain/repositories.h"
#include "storage/database_session.h"
#include <QSqlQuery>
namespace nekotune {
class QueueRepository final : public IQueueRepository {
  public:
    explicit QueueRepository(DatabaseSession &session) : m_session(session), m_db(session.database()) {}
    QString errorString() const override { return m_session.errorString(); }
    bool saveQueue(const QueueSnapshot &snapshot) override;
    QueueSnapshot loadQueue() const override;

  private:
    void setError(const QString &message) { m_session.setError(message); }
    DatabaseSession &m_session;
    QSqlDatabase m_db;
};
} // namespace nekotune
