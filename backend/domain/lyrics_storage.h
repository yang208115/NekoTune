#pragma once
#include "domain/lyrics_types.h"
#include "domain/result.h"
namespace nekotune {
class ILyricsStorage {
  public:
    virtual ~ILyricsStorage() = default;
    virtual Result<std::optional<LyricsDocument>> readLocal(const LyricsQuery &query,
                                                            const QString &path) const = 0;
    virtual std::optional<LyricsDocument> readCache(const LyricsQuery &query) const = 0;
    virtual bool writeCache(const LyricsQuery &query, const LyricsDocument &document) = 0;
};
} // namespace nekotune
