#pragma once
#include "domain/playback/playback_backend.h"
#include "domain/playback/playback_mode.h"
#include "domain/playback/player_queue.h"
namespace nekotune {
/// A value snapshot joins selection, decoder timing and display tags.
/// An absent song means the queue currently has no selected occurrence.
/// Stopped/paused describe playback state rather than file membership.
/// Zero duration is valid while the decoder has not supplied metadata.
/// Volume zero is a meaningful mute value, never a missing-value marker.
/// The IPC serializer converts this aggregate without exposing adapters.
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
