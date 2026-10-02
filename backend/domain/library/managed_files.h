#pragma once
#include "domain/result.h"
#include <QStringList>
#include <memory>

namespace nekotune {
/// The handle represents staged filesystem changes awaiting a DB commit.
/// Its lifetime is part of the collection deletion transaction.
/// Keeping it uncommitted makes early-return recovery automatic.
/// commit() ends restoration responsibility even when unlinking fails.
/// Its returned paths identify leftovers requiring user-visible notice.
/// They do not mean the corresponding library deletion was undone.
/// External source files must remain outside the removal set.
class IManagedFileRemoval {
  public:
    virtual ~IManagedFileRemoval() = default;
    // Until committed, destruction restores the staged files.
    virtual QStringList commit() = 0;
};
class IManagedFiles {
  public:
    virtual ~IManagedFiles() = default;
    /// Requires the caller's active database transaction. Retain the handle until commit succeeds;
    /// its destruction restores staged files, while commit returns any final cleanup failures.
    virtual Result<std::unique_ptr<IManagedFileRemoval>> stageRemoval(const QStringList &hashes) = 0;
};
} // namespace nekotune
