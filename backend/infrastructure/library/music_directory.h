#pragma once
#include "domain/library/library_types.h"
#include "domain/library/managed_files.h"
#include "domain/result.h"
#include "storage/database_session.h"

namespace nekotune {
class MusicDirectory final : public IManagedFiles {
  public:
    explicit MusicDirectory(DatabaseSession &database, const QString &directory = {});
    Result<ImportedFile> manage(const ImportedFile &file);
    Result<QString> reserveDownload(const QString &providerHash, const QString &title);
    Result<bool> ignored(const QString &hash) const;
    bool containsSong(const QString &hash) const;
    QString baseFor(const QString &hash) const;
    QString directory() const { return m_directory; }
    Result<std::unique_ptr<IManagedFileRemoval>> stageRemoval(const QStringList &hashes) override;

  private:
    Result<qint64> allocate(const QString &sourceName);
    QString base(qint64 id) const;
    DatabaseSession &m_database;
    QString m_directory;
};
} // namespace nekotune
