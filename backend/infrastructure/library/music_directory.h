#pragma once
#include "domain/library/library_types.h"
#include "domain/library/managed_files.h"
#include "domain/result.h"
#include "storage/database_session.h"
#include <functional>

namespace nekotune {
/// Maps audio hashes to numbered resources; external audio is linked or referenced, never copied.
/// Database access stays on the supplied session's owning backend thread.
class MusicDirectory final : public IManagedFiles {
  public:
    using LinkCreator = std::function<bool(const QString &, const QString &)>;
    explicit MusicDirectory(DatabaseSession &database, const QString &directory = {},
                            LinkCreator createLink = {});
    /// @param file Hashed source inspection result to register under its audio identity.
    /// @return Inspection metadata with a decoder-readable audio path and original source name.
    /// External audio uses a native symlink or a reference document on link creation failure.
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
    LinkCreator m_createLink;
};
} // namespace nekotune
