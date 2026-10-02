#pragma once
#include "domain/ai_backend.h"
#include "infrastructure/credentials/credential_store.h"
#include <QHash>
#include <QThread>

namespace nekotune {
class AiWorker;
class AiBackend final : public IAiBackend {
  public:
    explicit AiBackend(std::shared_ptr<CredentialStore> store = systemCredentialStore(),
                       int timeoutMs = 60000);
    ~AiBackend() override;
    void configuration(ConfigCompletion done) override;
    void configure(const AiConfigUpdate &update, ConfigCompletion done) override;
    void clearKey(ConfigCompletion done) override;
    void suggest(const AiMetadataInput &input, SuggestionCompletion done) override;
    void test(SuggestionCompletion done) override;
    void shutdown() override;

  private:
    void configOperation(std::function<Result<AiConfig>(AiWorker &)> operation, ConfigCompletion done);
    void generate(const AiMetadataInput &input, bool test, SuggestionCompletion done);
    QThread m_thread;
    AiWorker *m_worker;
    int m_timeoutMs;
    quint64 m_nextId = 0;
    bool m_stopped = false;
    QHash<quint64, ConfigCompletion> m_configPending;
    QHash<quint64, SuggestionCompletion> m_suggestionPending;
};
} // namespace nekotune
