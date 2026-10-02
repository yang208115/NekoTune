#pragma once
#include "domain/ai_backend.h"
#include "infrastructure/credential_store.h"
#include <QJsonObject>

namespace nekotune {
// Used only on the AI worker thread; native keychain operations may block.
class AiSettings final {
  public:
    explicit AiSettings(std::shared_ptr<CredentialStore> store) : m_store(std::move(store)) {}
    AiConfig configuration() const;
    Result<AiConfig> configure(const AiConfigUpdate &update);
    Result<AiConfig> clearKey();
    Result<QByteArray> key(const AiConfig &config) const;
    static Result<QString> normalizeBaseUrl(const QString &value);

  private:
    static QString endpointId(const QString &baseUrl);
    static QString credentialId(const QString &baseUrl);
    std::shared_ptr<CredentialStore> m_store;
};
} // namespace nekotune
