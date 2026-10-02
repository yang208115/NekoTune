#pragma once
#include "domain/ai_backend.h"
#include "infrastructure/credentials/credential_store.h"
#include <QHash>
#include <QThread>

namespace nekotune {
class AiWorker;
/// Provides asynchronous config/generation from an isolated AI thread.
/// Pending callback maps belong to the calling backend thread.
/// Workers receive copied input and return values through queued calls.
/// Suggestions have a user-visible deadline even if the keyring stalls.
/// A bounded number of in-flight generations prevents runaway request state.
/// Shutdown resolves callbacks once and joins the worker before destruction.
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
