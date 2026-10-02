#pragma once
#include <QString>
#include <optional>

namespace nekotune {
// Modes govern the next selection, not source loading or progress.
// Changing mode keeps the currently audible track in place.
// RepeatOne repeats on natural end; manual Next still advances.
// Sequential stops at the end, whereas RepeatAll wraps the list.
// Shuffle uses occurrence identities and remembered navigation history.
// String values below are shared by settings, IPC and QML controls.
enum class PlaybackMode { Sequential, RepeatOne, Shuffle, RepeatAll };
inline QString toString(PlaybackMode mode) {
    switch (mode) {
    case PlaybackMode::Sequential: return QStringLiteral("sequential");
    case PlaybackMode::RepeatOne: return QStringLiteral("repeat_one");
    case PlaybackMode::Shuffle: return QStringLiteral("shuffle");
    case PlaybackMode::RepeatAll: return QStringLiteral("repeat_all");
    }
    return QStringLiteral("sequential");
}
// Return no value for unknown strings instead of silently coercing them.
// API validation can reject bad input while startup chooses a fallback.
// This distinction prevents corrupted preferences from breaking startup
// without accepting invalid user commands as successful mode changes.
inline std::optional<PlaybackMode> playbackModeFromString(const QString &value) {
    for (auto mode : {PlaybackMode::Sequential, PlaybackMode::RepeatOne,
                      PlaybackMode::Shuffle, PlaybackMode::RepeatAll})
        if (toString(mode) == value)
            return mode;
    return {};
}
} // namespace nekotune
