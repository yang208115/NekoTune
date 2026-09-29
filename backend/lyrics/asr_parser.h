#pragma once

#include "lyrics/lyrics_types.h"
#include <QByteArray>

namespace nekotune {

class AsrParser final {
  public:
    static QVector<LyricLine> parse(const QByteArray &json);
    // Persist only validated lyric data, excluding URLs and recognition metadata.
    static QString serialize(const QVector<LyricLine> &lines);
};

} // namespace nekotune
