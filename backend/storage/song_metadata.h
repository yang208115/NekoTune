#pragma once

#include <QString>

namespace nekotune {

struct SongMetadata {
    int id = 0;
    QString hash;
    QString firstPath;
    QString customTitle;
    QString artist;
    QString lyrics;
};

} // namespace nekotune
