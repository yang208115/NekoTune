#pragma once
#include "domain/result.h"
#include <QByteArray>
#include <memory>
#include <optional>

namespace nekotune {
class CredentialStore {
  public:
    virtual ~CredentialStore() = default;
    /// An empty optional means absent; a locked/unavailable keyring is an error, not an absent secret.
    virtual Result<std::optional<QByteArray>> read(const QString &id) = 0;
    /// @param id Opaque credential namespace, not a destination filesystem path.
    /// @param secret Raw bytes; implementations must preserve binary session payloads.
    /// Replace the native-store value or report failure without a plaintext fallback.
    /// Migration callers verify readback before deleting their original legacy source.
    virtual Result<void> write(const QString &id, const QByteArray &secret) = 0;
    /// Remove only the requested credential namespace; other keys and account sessions remain.
    /// An unavailable or locked store is an error rather than proof of successful clearing.
    /// Callers retain their saved-status state when removal cannot be confirmed.
    virtual Result<void> remove(const QString &id) = 0;
};
std::shared_ptr<CredentialStore> systemCredentialStore();
} // namespace nekotune
