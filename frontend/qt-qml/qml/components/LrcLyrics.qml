import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string lyrics: ""
    property real position: 0
    property var parsedLines: []
    property int activeIndex: -1
    property color activeColor: "#f7f4ed"
    property color inactiveColor: "#89929a"

    visible: root.lyrics.trim().length > 0
    clip: true

    onLyricsChanged: parseLyrics()
    onPositionChanged: updateActiveLine()
    onActiveIndexChanged: Qt.callLater(scrollToActive)

    ListView {
        id: lyricsList

        anchors.fill: parent
        clip: true
        spacing: 8
        model: root.parsedLines
        currentIndex: root.activeIndex

        onCountChanged: Qt.callLater(root.scrollToActive)

        delegate: Text {
            required property int index
            required property var modelData

            width: lyricsList.width
            text: modelData.text
            color: index === root.activeIndex ? root.activeColor : root.inactiveColor
            opacity: index === root.activeIndex ? 1.0 : 0.72
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: index === root.activeIndex ? 18 : 15
            font.weight: index === root.activeIndex ? Font.Bold : Font.Normal
        }

        add: Transition {
            NumberAnimation { properties: "opacity"; from: 0; to: 1; duration: 120 }
        }

        displaced: Transition {
            NumberAnimation { properties: "y"; duration: 120 }
        }
    }

    function parseLyrics() {
        const result = []
        let offsetMs = 0
        const sourceLines = root.lyrics.split(/\r?\n/)
        const timestampPattern = /\[(\d+):(\d{2})(?:[.:](\d{1,3}))?\]/g

        for (let lineIndex = 0; lineIndex < sourceLines.length; lineIndex += 1) {
            const sourceLine = sourceLines[lineIndex]
            const offsetMatch = sourceLine.match(/^\[offset:([+-]?\d+)\]/i)
            if (offsetMatch) {
                offsetMs = Number(offsetMatch[1])
                continue
            }

            const timestamps = []
            let match
            while ((match = timestampPattern.exec(sourceLine)) !== null) {
                const fraction = match[3] || "0"
                const fractionMs = Number(fraction) * Math.pow(10, 3 - fraction.length)
                timestamps.push((Number(match[1]) * 60 + Number(match[2])) * 1000
                                + fractionMs + offsetMs)
            }
            timestampPattern.lastIndex = 0

            const text = sourceLine.replace(/\[\d+:\d{2}(?:[.:]\d{1,3})?\]/g, "").trim()
            if (!text || /^\[(ar|al|by|re|ti|ve):/i.test(text)) {
                continue
            }

            if (timestamps.length === 0) {
                result.push({timeMs: -1, text: text})
                continue
            }

            for (let timestampIndex = 0; timestampIndex < timestamps.length; timestampIndex += 1) {
                result.push({timeMs: timestamps[timestampIndex], text: text})
            }
        }

        result.sort((left, right) => left.timeMs - right.timeMs)
        root.parsedLines = result
        updateActiveLine()
    }

    function updateActiveLine() {
        const playbackPosition = Number(root.position || 0)
        let nextActiveIndex = -1
        for (let index = 0; index < root.parsedLines.length; index += 1) {
            const line = root.parsedLines[index]
            if (line.timeMs >= 0 && line.timeMs <= playbackPosition) {
                nextActiveIndex = index
            }
        }

        if (root.activeIndex !== nextActiveIndex) {
            root.activeIndex = nextActiveIndex
        }
    }

    function scrollToActive() {
        if (root.activeIndex >= 0 && root.activeIndex < lyricsList.count) {
            lyricsList.positionViewAtIndex(root.activeIndex, ListView.Center)
        }
    }
}
