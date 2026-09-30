pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var translator: null
    property var lyrics: ({})
    property var song: ({})
    property real position: 0
    property real duration: 0
    property string playbackState: "stopped"
    signal seekRequested(real positionMs)
    readonly property var document: lyrics.document || ({})
    readonly property var lines: document.lines || []
    readonly property int activeIndex: findActiveIndex()
    readonly property var activeLine: activeIndex >= 0 && activeIndex < lines.length ? lines[activeIndex] : ({})
    readonly property var nextLine: activeIndex >= 0 && activeIndex + 1 < lines.length ? lines[activeIndex + 1] : ({})

    color: "#11151b"
    radius: 14
    border.color: "#e8a9c3"
    border.width: 1
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            Label {
                Layout.fillWidth: true
                text: root.tr("lyrics_debug_page")
                color: "#e8a9c3"
                font.bold: true
                font.pixelSize: 18
            }
            Label {
                text: "--lyrics-debug"
                color: "#89929a"
                font.pixelSize: 11
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 10
            rowSpacing: 3

            Label { text: "state"; color: "#89929a" }
            Label { Layout.fillWidth: true; text: root.playbackState; color: "#f7f4ed"; elide: Text.ElideRight }
            Label { text: "lyrics"; color: "#89929a" }
            Label { Layout.fillWidth: true; text: String(root.lyrics.state || "-") + " / " + String(root.document.source || "-"); color: "#f7f4ed"; elide: Text.ElideRight }

            Label { text: "position"; color: "#89929a" }
            Label { Layout.fillWidth: true; text: root.formatMs(root.position) + " (" + String(Math.round(root.position)) + " ms)"; color: "#f7f4ed"; elide: Text.ElideRight }
            Label { text: "duration"; color: "#89929a" }
            Label { Layout.fillWidth: true; text: root.formatMs(root.duration) + " (" + String(Math.round(root.duration)) + " ms)"; color: "#f7f4ed"; elide: Text.ElideRight }

            Label { text: "active"; color: "#89929a" }
            Label { Layout.fillWidth: true; text: String(root.activeIndex) + " @ " + root.lineTime(root.activeLine); color: "#f7f4ed"; elide: Text.ElideRight }
            Label { text: "next"; color: "#89929a" }
            Label { Layout.fillWidth: true; text: root.lineTime(root.nextLine) + " (Δ " + root.deltaToNext() + ")"; color: "#f7f4ed"; elide: Text.ElideRight }

            Label { text: "track"; color: "#89929a" }
            Label { Layout.columnSpan: 3; Layout.fillWidth: true; text: String(root.song.title || "-") + " · " + String(root.song.artist || "-"); color: "#f7f4ed"; elide: Text.ElideRight }
        }

        Label {
            Layout.fillWidth: true
            text: "Current: " + String(root.activeLine.text || "(none)")
            color: "#cbb8ff"
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }

        ListView {
            id: lineList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: root.lines
            currentIndex: root.activeIndex
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: lineRow
                required property var modelData
                required property int index
                objectName: "debugLine" + index
                width: lineList.width - 12
                height: 26
                radius: 5
                color: lineRow.index === root.activeIndex ? "#49384b" : "transparent"

                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.seekRequested(Number(lineRow.modelData.time_ms)) }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 8
                    Label {
                        Layout.preferredWidth: 86
                        text: String(lineRow.modelData.time_ms) + " ms"
                        color: lineRow.index === root.activeIndex ? "#e8a9c3" : "#89929a"
                        font.pixelSize: 11
                    }
                    Label {
                        Layout.fillWidth: true
                        text: lineRow.modelData.text
                        color: lineRow.index === root.activeIndex ? "#f7f4ed" : "#a7adb3"
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                }
            }

            onCurrentIndexChanged: {
                if (currentIndex >= 0 && currentIndex < count)
                    positionViewAtIndex(currentIndex, ListView.Contain)
            }
        }
    }

    function tr(key) {
        return root.translator ? root.translator.text(key, root.translator.language) : key
    }

    function findActiveIndex() {
        let low = 0
        let high = root.lines.length
        while (low < high) {
            const mid = Math.floor((low + high) / 2)
            if (Number(root.lines[mid].time_ms) <= Number(root.position || 0))
                low = mid + 1
            else
                high = mid
        }
        return low - 1
    }

    function lineTime(line) {
        return line && line.time_ms !== undefined ? String(line.time_ms) + " ms" : "-"
    }

    function deltaToNext() {
        if (!root.nextLine || root.nextLine.time_ms === undefined)
            return "-"
        return String(Math.round(Number(root.nextLine.time_ms) - Number(root.position || 0))) + " ms"
    }

    function formatMs(value) {
        const total = Math.max(0, Math.floor(Number(value || 0)))
        const seconds = Math.floor(total / 1000)
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0") + "." + String(total % 1000).padStart(3, "0")
    }
}
