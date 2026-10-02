#pragma once

#include "domain/lyrics/lyrics_types.h"

#include <QString>

#include <optional>

namespace nekotune {

class LyricsCache final {
  public:
    explicit LyricsCache(const QString &directory = {});

    QString directory() const;
    QString keyFor(const LyricsQuery &query) const;
    std::optional<LyricsDocument> read(const LyricsQuery &query) const;
    bool write(const LyricsQuery &query, const LyricsDocument &document) const;

  private:
    QString m_directory;
};

} // namespace nekotune
