#pragma once
#include <QString>
#include <optional>

namespace nekotune {
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
inline std::optional<PlaybackMode> playbackModeFromString(const QString &value) {
    for (auto mode : {PlaybackMode::Sequential, PlaybackMode::RepeatOne,
                      PlaybackMode::Shuffle, PlaybackMode::RepeatAll})
        if (toString(mode) == value)
            return mode;
    return {};
}
} // namespace nekotune
