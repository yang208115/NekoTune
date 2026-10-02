#pragma once
#include "domain/ai_backend.h"
#include "infrastructure/credentials/credential_store.h"
#include <QJsonObject>

namespace nekotune {
// Used only on the AI worker thread; native keychain operations may block.
/// Normal settings store only endpoint, model and credential-presence markers.
/// Actual key bytes are isolated in the injected system credential store.
/// Profile and normalized endpoint jointly namespace each secret.
/// Configure validates input before replacing either persistence component.
/// When settings fail after a key write, restore the previous credential.
/// Native-store errors must not enable an insecure plaintext fallback.
class AiSettings final {
  public:
    explicit AiSettings(std::shared_ptr<CredentialStore> store) : m_store(std::move(store)) {}
    AiConfig configuration() const;
    /// @param update Endpoint/model fields plus optional key replacement.
    /// An omitted key preserves the saved secret; configuration never returns its bytes.
    /// Validate the proposed endpoint before moving to its credential namespace.
    /// If settings persistence fails after a secret change, attempt to restore the previous key.
    Result<AiConfig> configure(const AiConfigUpdate &update);
    Result<AiConfig> clearKey();
    /// Read the key for the supplied normalized endpoint and active profile.
    /// This worker-only lookup is separate from public configuration serialization.
    /// Keyless configurations return empty bytes; missing saved keys and store failures are errors.
    Result<QByteArray> key(const AiConfig &config) const;
    /// Accept absolute HTTP/HTTPS endpoints; transport policy is not upgraded by normalization.
    /// Reject credentials, query and fragment components before constructing request URLs.
    /// Normalization keeps endpoint prefix paths for chat-completions request construction.
    static Result<QString> normalizeBaseUrl(const QString &value);

  private:
    static QString endpointId(const QString &baseUrl);
    static QString credentialId(const QString &baseUrl);
    std::shared_ptr<CredentialStore> m_store;
};
} // namespace nekotune
