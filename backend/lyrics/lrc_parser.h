#pragma once

#include "lyrics/lyrics_types.h"

namespace nekotune {

class LrcParser final {
  public:
    static QVector<LyricLine> parse(const QString &lyrics);
    static bool looksLikeLrc(const QString &lyrics);
    static QJsonArray toJson(const QVector<LyricLine> &lines);
};

} // namespace nekotune
