#pragma once
#include <QString>
#include <atomic>

namespace nekotune {
// Returns milliseconds, or zero when the local audio cannot be inspected.
/// Read local audio duration without requiring a live playback session.
/// Zero represents unavailable/probe-failed duration, not a zero-length song.
/// The cancellation flag is shared with the import worker's shutdown path.
/// FFmpeg probing also has a bounded interrupt deadline and format list.
/// Only timing metadata is inspected; audio is not decoded for playback.
qint64 readAudioDuration(const QString &path,
                         const std::atomic_bool &cancelled);
} // namespace nekotune
