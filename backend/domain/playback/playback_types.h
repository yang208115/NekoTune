#pragma once
#include "domain/playback/playback_backend.h"
#include "domain/playback/playback_mode.h"
#include "domain/playback/player_queue.h"
namespace nekotune {
struct PlaybackSnapshot {
    PlayerState state = PlayerState::Stopped;
    qint64 position = 0;
    qint64 duration = 0;
    double volume = .8;
    std::optional<QueueItem> song;
    AudioMetadata metadata;
    PlaybackMode playbackMode = PlaybackMode::Sequential;
};
} // namespace nekotune
