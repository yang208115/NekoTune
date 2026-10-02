#pragma once
#include "application/library/library_service.h"
#include "domain/ai_backend.h"

namespace nekotune {
/// Builds an AI input snapshot from a validated library song and draft.
/// Draft values are not persisted by suggestion generation.
/// Existing tag names supply canonical categories for model output.
/// Only the backend adapter accesses credentials and network endpoints.
/// The caller receives an advisory result for an explicit editor save.
class AiService final {
  public:
    AiService(IAiBackend &backend, LibraryService &library, ITagRepository &tags)
        : m_backend(backend), m_library(library), m_tags(tags) {}
    void configuration(IAiBackend::ConfigCompletion done) { m_backend.configuration(std::move(done)); }
    void configure(const AiConfigUpdate &update, IAiBackend::ConfigCompletion done) {
        m_backend.configure(update, std::move(done));
    }
    void clearKey(IAiBackend::ConfigCompletion done) { m_backend.clearKey(std::move(done)); }
    void test(IAiBackend::SuggestionCompletion done) { m_backend.test(std::move(done)); }
    void suggest(int songId, const MetadataPatch &draft, IAiBackend::SuggestionCompletion done);

  private:
    IAiBackend &m_backend;
    LibraryService &m_library;
    ITagRepository &m_tags;
};
} // namespace nekotune
