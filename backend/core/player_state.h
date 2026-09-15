#pragma once

#include <QString>

namespace nekotune {

enum class PlayerState {
    Stopped,
    Playing,
    Paused,
    Loading,
    Error,
};

inline QString toString(PlayerState state)
{
    switch (state) {
    case PlayerState::Stopped:
        return QStringLiteral("stopped");
    case PlayerState::Playing:
        return QStringLiteral("playing");
    case PlayerState::Paused:
        return QStringLiteral("paused");
    case PlayerState::Loading:
        return QStringLiteral("loading");
    case PlayerState::Error:
        return QStringLiteral("error");
    }

    return QStringLiteral("error");
}

} // namespace nekotune
