#pragma once
#include "domain/result.h"
#include <QByteArray>
#include <memory>
#include <optional>

namespace nekotune {
class CredentialStore {
  public:
    virtual ~CredentialStore() = default;
    virtual Result<std::optional<QByteArray>> read(const QString &id) = 0;
    virtual Result<void> write(const QString &id, const QByteArray &secret) = 0;
    virtual Result<void> remove(const QString &id) = 0;
};
std::shared_ptr<CredentialStore> systemCredentialStore();
} // namespace nekotune
