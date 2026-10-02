#pragma once
#include "domain/result.h"
#include <QStringList>
#include <memory>

namespace nekotune {
class IManagedFileRemoval {
  public:
    virtual ~IManagedFileRemoval() = default;
    // Until committed, destruction restores the staged files.
    virtual QStringList commit() = 0;
};
class IManagedFiles {
  public:
    virtual ~IManagedFiles() = default;
    virtual Result<std::unique_ptr<IManagedFileRemoval>> stageRemoval(const QStringList &hashes) = 0;
};
} // namespace nekotune
