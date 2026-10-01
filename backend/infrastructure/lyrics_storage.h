#pragma once
#include "domain/lyrics_storage.h"
#include "infrastructure/lyrics_cache.h"
namespace nekotune {
class LyricsStorage final : public ILyricsStorage {
  public:
    explicit LyricsStorage(const QString &directory = {}) : m_cache(directory) {}
    Result<std::optional<LyricsDocument>> readLocal(const LyricsQuery &query,
                                                    const QString &path) const override;
    std::optional<LyricsDocument> readCache(const LyricsQuery &query) const override {
        return m_cache.read(query);
    }
    bool writeCache(const LyricsQuery &query, const LyricsDocument &document) override {
        return m_cache.write(query, document);
    }

  private:
    LyricsCache m_cache;
};
} // namespace nekotune
