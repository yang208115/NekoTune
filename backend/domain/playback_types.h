#pragma once
#include "domain/playback_backend.h"
#include "domain/player_queue.h"
namespace nekotune {
struct PlaybackSnapshot {
    PlayerState state = PlayerState::Stopped;
    qint64 position = 0;
    qint64 duration = 0;
    double volume = .8;
    std::optional<QueueItem> song;
    AudioMetadata metadata;
    QString coverUrl;
};
} // namespace nekotune
