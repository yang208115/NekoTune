pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Consumes already parsed absolute line/word times from backend snapshots.
// LRC lines follow backend position; KRC can interpolate brief visual progress.
// Interpolation is bounded and re-anchored by every playback update.
// Manual scrolling suspends automatic follow so readers can inspect other lines.
// Seek requests use the line's timestamp rather than its list index.
// Plain lyrics use a separate wrapped view without timing selection.
Item {
    id: root

    property var lines: []
    property string plainText: ""
    property real position: 0
    property bool playing: false
    property int activeIndex: -1
    readonly property real activeTime: activeIndex >= 0 ? Number(lines[activeIndex].time_ms) : -1
    readonly property bool hasWordTiming: lines && lines.length > 0 && Boolean(lines[0].words)
    property real displayPosition: position
    property real anchorPosition: position
    property real anchorClock: 0
    readonly property real shownPosition: playing && hasWordTiming && !reducedMotion ? displayPosition : position
    property color activeColor: Theme.textPrimary
    property color nearColor: Theme.textSecondary
    property color inactiveColor: Theme.textMuted
    property bool reducedMotion: Theme.reducedMotion

    property bool userScrolling: false
    property int lastAutoIndex: -1

    signal seekRequested(real positionMs)

    clip: true

    Timer {
        id: resumeAutoScrollTimer
        interval: 3500
        repeat: false
        onTriggered: {
            root.userScrolling = false
            root.scrollToActive(true)
        }
    }

    Timer {
        interval: 16
        repeat: true
        running: root.visible && root.playing && root.hasWordTiming && !root.reducedMotion
        onTriggered: {
            // Only extrapolate briefly; a paused or delayed backend must not drift away.
            const elapsed = Math.min(120, Date.now() - root.anchorClock)
            root.displayPosition = Math.max(root.displayPosition, root.anchorPosition + elapsed)
        }
    }

    onLinesChanged: {
        followScroll.stop()
        lastAutoIndex = -1
        displayPosition = position
        updateActiveLine()
        if (activeIndex < 0) lyricsList.positionViewAtBeginning()
    }
    onPlayingChanged: {
        displayPosition = position
        anchorPosition = position
        anchorClock = Date.now()
    }
    onPositionChanged: {
        // Preserve interpolation for small updates, but snap on seeks or backward jumps to resync words.
        if (!playing || Math.abs(position - displayPosition) > 250 || position < anchorPosition - 40)
            displayPosition = position
        anchorPosition = position
        anchorClock = Date.now()
        updateActiveLine()
    }
    onShownPositionChanged: updateActiveLine()
    onActiveIndexChanged: {
        if (!root.userScrolling) {
            Qt.callLater(scrollToActive)
        }
    }

    ListView {
        id: lyricsList
        objectName: "lyricsList"
        anchors.fill: parent
        anchors.topMargin: 20
        anchors.bottomMargin: 20
        clip: true
        spacing: 16
        cacheBuffer: Math.max(0, height)
        visible: root.lines && root.lines.length > 0
        model: root.lines
        currentIndex: -1
        boundsBehavior: Flickable.DragOverBounds

        NumberAnimation {
            id: followScroll
            target: lyricsList
            property: "contentY"
            duration: root.reducedMotion ? 0 : Theme.durationScroll
            easing.type: Easing.OutCubic
        }

        onDraggingChanged: {
            if (dragging) {
                followScroll.stop()
                root.userScrolling = true
                resumeAutoScrollTimer.restart()
            }
        }
        onFlickingChanged: {
            if (flicking) {
                followScroll.stop()
                root.userScrolling = true
                resumeAutoScrollTimer.restart()
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            width: 4
            contentItem: Rectangle { radius: 2; color: Theme.borderSubtle }
        }

        delegate: Item {
            id: lineDelegate
            required property var modelData
            required property int index

            objectName: "lyricLine" + index
            readonly property bool active: index === root.activeIndex
            readonly property int distanceToActive: Math.abs(index - root.activeIndex)
            readonly property real timeMs: Number(modelData.time_ms || 0)
            readonly property var words: modelData.words || []
            readonly property bool hasKrc: words.length > 0
            readonly property int activeWordIndex: active ? root.wordIndex(words, root.shownPosition) : -1

            width: lyricsList.width
            height: Math.max(40, (hasKrc ? wordFlow.height : lineText.implicitHeight) + 12)

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSm
                color: lineMouse.hovered ? Theme.bgHover : "transparent"
            }

            Text {
                id: lineText
                anchors.centerIn: parent
                width: parent.width - 32
                objectName: "lyricText" + lineDelegate.index
                visible: !lineDelegate.hasKrc
                text: lineDelegate.modelData.text || "♪"
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: root.width < 500 ? Theme.fontLyricsCompact : Theme.fontLyricsWide
                font.weight: lineDelegate.active ? Font.DemiBold : Font.Normal
                lineHeightMode: Text.ProportionalHeight
                lineHeight: Theme.lineHeightLyrics
                color: lineDelegate.active ? root.activeColor
                       : lineMouse.hovered ? Theme.textPrimary
                       : (lineDelegate.distanceToActive <= 1 ? root.nearColor : root.inactiveColor)
                opacity: 1.0

                Behavior on color {
                    ColorAnimation { duration: root.reducedMotion ? 0 : Theme.durationFast }
                }
            }

            Flow {
                id: wordFlow
                objectName: "krcLine" + lineDelegate.index
                visible: lineDelegate.hasKrc
                // Whole-line metrics can omit trailing spaces and differ from separately shaped words.
                readonly property real unwrappedWidth: {
                    let total = 0
                    for (const child of children) {
                        if (child !== wordRepeater) total += child.width
                    }
                    return total
                }
                width: Math.max(1, Math.min(parent.width - 32, Math.ceil(unwrappedWidth)))
                height: childrenRect.height
                x: (parent.width - width) / 2
                y: (parent.height - height) / 2
                spacing: 0
                // Line focus follows the timestamp immediately; only word reveal and scrolling interpolate.
                opacity: lineDelegate.active ? 1 : lineDelegate.distanceToActive <= 1 ? 0.82 : 0.62

                Repeater {
                    id: wordRepeater
                    model: lineDelegate.words
                    delegate: Item {
                        id: wordItem
                        required property var modelData
                        required property int index
                        readonly property real progress: root.wordProgress(modelData, root.shownPosition)

                        objectName: "krcWord" + lineDelegate.index + "_" + index
                        width: baseWord.implicitWidth
                        height: baseWord.implicitHeight * 1.35

                        Text {
                            id: baseWord
                            objectName: "krcBaseText" + lineDelegate.index + "_" + wordItem.index
                            anchors.verticalCenter: parent.verticalCenter
                            text: wordItem.modelData.text
                            textFormat: Text.PlainText
                            font.pixelSize: root.width < 500 ? 20 : 24
                            font.weight: Font.DemiBold
                            color: lineDelegate.active ? root.inactiveColor
                                   : lineDelegate.distanceToActive <= 1 ? root.nearColor : root.inactiveColor

                        }

                        Item {
                            // Reveal a second identically shaped plain-text layer through a clipping rectangle.
                            // Animating the clip width keeps word layout and line height stable while color advances.
                            // Changing font weight or replacing text for progress would trigger unwanted reflow.
                            id: reveal
                            objectName: "krcReveal" + lineDelegate.index + "_" + wordItem.index
                            width: wordItem.width * wordItem.progress
                            height: wordItem.height
                            clip: true

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: wordItem.modelData.text
                                textFormat: Text.PlainText
                                font.pixelSize: root.width < 500 ? 20 : 24
                                font.weight: Font.DemiBold
                                color: lineDelegate.active ? root.activeColor
                                       : lineDelegate.distanceToActive <= 1 ? root.nearColor : root.inactiveColor

                            }
                        }
                    }
                }
            }

            HoverHandler {
                id: lineMouse
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: {
                    if (lineDelegate.timeMs >= 0) {
                        root.userScrolling = false
                        resumeAutoScrollTimer.stop()
                        followScroll.stop()
                        // The clicked row is already positioned by this interaction.
                        // Remember its index so the arriving seek acknowledgment does not start another follow animation.
                        // Manual seeking also clears the temporary user-scroll suspension above.
                        root.lastAutoIndex = lineDelegate.index
                        root.seekRequested(lineDelegate.timeMs)
                        lyricsList.positionViewAtIndex(lineDelegate.index, ListView.Center)
                    }
                }
            }
        }
    }

    // Top fade overlay (events pass through!)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 36
        enabled: false
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.bgCanvas }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Bottom fade overlay (events pass through!)
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 36
        enabled: false
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.bgCanvas }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 16
        visible: (!root.lines || root.lines.length === 0) && root.plainText.length > 0
        contentWidth: availableWidth

        TextArea {
            text: root.plainText
            textFormat: TextEdit.PlainText
            readOnly: true
            wrapMode: TextEdit.Wrap
            color: Theme.textSecondary
            font.pixelSize: 15
            horizontalAlignment: TextEdit.AlignHCenter
            background: null
        }
    }

    function updateActiveLine() {
        if (!root.lines || root.lines.length === 0) {
            root.activeIndex = -1
            return
        }
        // Upper bound chooses the last line at/before playback, including duplicate timestamps;
        // before the first timestamp, low - 1 deliberately leaves every line inactive.
        let low = 0
        let high = root.lines.length
        while (low < high) {
            const mid = Math.floor((low + high) / 2)
            if (Number(root.lines[mid].time_ms) <= root.shownPosition)
                low = mid + 1
            else
                high = mid
        }
        root.activeIndex = low - 1
    }

    // Word intervals are half-open: a word stops being active at its exact end timestamp.
    // Gaps between intervals therefore legitimately have no active word.
    // Revealed progress is computed separately so completed words stay visible during those gaps.
    function wordIndex(words, timeMs) {
        if (!words || words.length === 0) return -1
        for (let index = 0; index < words.length; index += 1) {
            const start = Number(words[index].time_ms)
            if (start <= timeMs && timeMs < start + Number(words[index].duration_ms)) return index
        }
        return -1
    }

    // Clamp extrapolated media position to a visible reveal fraction between zero and one.
    // A zero-duration marker changes instantaneously at its timestamp instead of dividing by zero.
    // Backward seeking recomputes this fraction directly, without preserving old reveal state.
    function wordProgress(word, timeMs) {
        const start = Number(word.time_ms)
        const duration = Number(word.duration_ms)
        if (duration <= 0) return timeMs >= start ? 1 : 0
        return Math.max(0, Math.min(1, (timeMs - start) / duration))
    }

    function scrollToActive(forceAnimation) {
        if (root.userScrolling || root.activeIndex < 0 || root.activeIndex >= lyricsList.count)
            return
        // Track the previous automatic target separately from the active timestamp lookup.
        // Only adjacent changes normally animate; a large seek can require direct delegate positioning.
        // Repeated updates within one lyric line must not restart the same scrolling animation.
        const previous = root.lastAutoIndex
        root.lastAutoIndex = root.activeIndex
        if (previous === root.activeIndex && !forceAnimation) return
        const item = lyricsList.itemAtIndex(root.activeIndex)
        const adjacent = Math.abs(root.activeIndex - previous) === 1
        // Large seeks may target an uninstantiated delegate; let ListView position it directly.
        if (!root.reducedMotion && item && (adjacent || forceAnimation)) {
            const targetY = Math.max(0, Math.min(item.y + item.height / 2 - lyricsList.height * 0.42,
                                                Math.max(0, lyricsList.contentHeight - lyricsList.height)))
            followScroll.stop()
            followScroll.from = lyricsList.contentY
            followScroll.to = targetY
            followScroll.start()
        } else {
            followScroll.stop()
            lyricsList.positionViewAtIndex(root.activeIndex, ListView.Center)
        }
    }
}
