#pragma once
#include "domain/repositories.h"
#include <QSqlDatabase>
namespace nekotune {
/// Owns one SQLite connection and a non-nested transaction; repositories share its owning thread.
/// All queries and copied QSqlDatabase handles must be destroyed before this session.
class DatabaseSession final : public ITransaction {
  public:
    explicit DatabaseSession(const QString &path = defaultDatabasePath(), const QString &connectionName = {});
    ~DatabaseSession() override;
    DatabaseSession(const DatabaseSession &) = delete;
    DatabaseSession &operator=(const DatabaseSession &) = delete;
    bool isReady() const { return m_ready; }
    QString errorString() const override { return m_error; }
    QString databasePath() const { return m_databasePath; }
    QSqlDatabase database() const { return m_db; }
    void setError(const QString &message) { m_error = message; }
    bool begin() override;
    bool commit() override;
    void rollback() override;
    static QString defaultDatabasePath();

  private:
    bool initialize(const QString &databasePath);
    bool migrate();
    bool migrateFolders();
    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_databasePath;
    QString m_error;
    bool m_ready = false;
    bool m_transactionActive = false;
};
} // namespace nekotune
