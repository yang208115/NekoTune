#pragma once
#include "domain/library/library_types.h"
#include "domain/library/managed_files.h"
#include "domain/result.h"
#include "storage/database_session.h"

namespace nekotune {
/// Maps audio hashes to numbered resources; external audio is linked, while sidecars are app-owned.
/// Database access stays on the supplied session's owning backend thread.
class MusicDirectory final : public IManagedFiles {
  public:
    explicit MusicDirectory(DatabaseSession &database, const QString &directory = {});
    /// @param file Hashed source inspection result to register under its audio identity.
    /// @return Inspection metadata with the managed audio path and original source name.
    /// External audio is linked rather than copied or renamed into app ownership.
    /// Known hashes reuse their persistent resource number across restarts.
    Result<ImportedFile> manage(const ImportedFile &file);
    /// Reserves a basename before audio SHA-256 is known; provider hashes are a separate identity.
    /// @param providerHash Provider search identity, which is not an audio SHA-256.
    /// @param title Display name associated with the reserved numbered resource.
    /// @return Basename for a future managed download, without its audio extension.
    /// Registration under the real audio hash occurs only after successful inspection.
    Result<QString> reserveDownload(const QString &providerHash, const QString &title);
    /// Read durable scan suppression for an audio hash deleted from the library.
    /// False means eligible for scanning; a database error is a distinct failed Result.
    /// Manual import is responsible for restoring deliberately re-added content.
    Result<bool> ignored(const QString &hash) const;
    bool containsSong(const QString &hash) const;
    QString baseFor(const QString &hash) const;
    QString directory() const { return m_directory; }
    /// @param hashes Audio identities whose registered owned assets should be staged.
    /// @return Rollback handle restoring staged names unless its commit is invoked.
    /// Only known managed assets are eligible; linked directories and foreign files are rejected.
    /// The caller commits this handle after its cross-collection database transaction succeeds.
    Result<std::unique_ptr<IManagedFileRemoval>> stageRemoval(const QStringList &hashes) override;

  private:
    Result<qint64> allocate(const QString &sourceName);
    QString base(qint64 id) const;
    DatabaseSession &m_database;
    QString m_directory;
};
} // namespace nekotune
