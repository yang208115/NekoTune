#pragma once
#include "domain/result.h"

namespace nekotune {
struct AudioReference {
    QString path;
    QString hash;
    QString sourceName;
};
/// References are bounded, versioned files in the numbered resource layout.
Result<AudioReference> readAudioReference(const QString &path);
bool writeAudioReference(const QString &path, const AudioReference &reference);
bool createAudioSymlink(const QString &source, const QString &target);
} // namespace nekotune
