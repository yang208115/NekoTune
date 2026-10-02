#pragma once
#include <QString>
#include <atomic>

namespace nekotune {
// Returns milliseconds, or zero when the local audio cannot be inspected.
qint64 readAudioDuration(const QString &path,
                         const std::atomic_bool &cancelled);
} // namespace nekotune
