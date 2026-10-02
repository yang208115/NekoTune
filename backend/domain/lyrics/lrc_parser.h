#pragma once

#include "domain/lyrics/lyrics_types.h"

namespace nekotune {

/// Accept timestamped text with a file-wide offset and repeated times.
/// Malformed rows are ignored rather than aborting the whole document.
/// Fractions normalize to milliseconds independent of digit count.
/// Output is chronologically sorted while preserving equal-time order.
/// looksLikeLrc checks usable lyric rows, not merely bracket syntax.
/// This lets custom/plain lyric text keep its correct display format.
class LrcParser final {
  public:
    static QVector<LyricLine> parse(const QString &lyrics);
    static bool looksLikeLrc(const QString &lyrics);
};

} // namespace nekotune
