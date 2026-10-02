import QtQuick

// Preview dragging locally and commit an authoritative seek on release.
// live=false means value may lag the visible handle while pressed.
// Pending seek shields the target from already-queued old progress events.
// A short acknowledgement window releases that shield if playback catches up.
// Duration changes invalidate pending targets from the previous media source.
PlayerSlider {
    id: slider

    property real playbackPosition: 0
    property real duration: 0
    property bool pendingSeek: false
    property real pendingSeekPosition: 0

    signal seekRequested(real positionMs)

    from: 0
    to: Math.max(duration, 1)
    enabled: duration > 0
    live: false

    // Playback updates must not move the handle while the user is scrubbing.
    // Sync on updates rather than release, so an old position cannot undo a seek.
    function syncPlaybackPosition() {
        if (!pressed && !pendingSeek)
            value = Math.max(from, Math.min(playbackPosition, to))
    }

    function commitSeek() {
        pendingSeekPosition = value
        pendingSeek = true
        pendingSeekTimeout.restart()
        seekRequested(pendingSeekPosition)
    }

    Component.onCompleted: syncPlaybackPosition()
    onDurationChanged: {
        pendingSeek = false
        pendingSeekTimeout.stop()
        syncPlaybackPosition()
    }

    Timer {
        id: pendingSeekTimeout
        interval: 750
        repeat: false
        onTriggered: {
            pendingSeek = false
            syncPlaybackPosition()
        }
    }

    // Old playback updates may still be queued when the user releases the
    // handle. Keep the target visible until playback catches up or times out.
    onPlaybackPositionChanged: {
        if (pendingSeek && Math.abs(playbackPosition - pendingSeekPosition) <= 250) {
            pendingSeek = false
            pendingSeekTimeout.stop()
        }
        syncPlaybackPosition()
    }

    // With live=false, moved can fire with the old value, and release need not
    // emit moved at all. The committed value is available when pressed clears.
    onPressedChanged: {
        if (pressed) {
            pendingSeek = false
            pendingSeekTimeout.stop()
        } else {
            commitSeek()
        }
    }
    onMoved: {
        if (!pressed)
            commitSeek()
    }
}
