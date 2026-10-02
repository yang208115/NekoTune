#pragma once
#include "domain/lyrics/lyrics_storage.h"
#include "infrastructure/lyrics/lyrics_cache.h"
namespace nekotune {
/// Combines local-sidecar lookup with the persistent document cache.
/// Local reads return a failure for authoritative unusable LRC files.
/// Cache reads instead treat invalid entries as ordinary cache misses.
/// Managed sidecars are checked before following an audio symlink.
/// This preserves program-owned selections over original-directory files.
/// The implementation performs no online lookup during local reads.
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
