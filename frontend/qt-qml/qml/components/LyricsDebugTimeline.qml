pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var document: ({})
    property real position: 0
    property string title: ""
    property string emptyText: ""
    readonly property var lines: document.lines || []
    readonly property int activeIndex: findActiveIndex()
    readonly property var activeLine: activeIndex >= 0 ? lines[activeIndex] : ({})
    signal seekRequested(real positionMs)

    color: "#171b23"
    radius: 8

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        Label {
            Layout.fillWidth: true
            text: root.title + " · " + String(root.document.source || "—") + " · " + root.lines.length
            color: "#e8a9c3"
            font.bold: true
            elide: Text.ElideRight
        }
        Label {
            Layout.fillWidth: true
            text: root.activeIndex >= 0 ? "#" + root.activeIndex + " @ " + root.activeLine.time_ms + " ms" : "—"
            color: "#cbb8ff"
            font.pixelSize: 11
        }
        ListView {
            id: timeline
            objectName: "timeline"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.lines
            currentIndex: root.activeIndex
            clip: true
            spacing: 4
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                objectName: "debugLine" + index
                width: timeline.width - 12
                height: rowContent.implicitHeight + 12
                radius: 5
                color: index === root.activeIndex ? "#49384b" : "transparent"
                ColumnLayout {
                    id: rowContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    spacing: 3
                    Label {
                        Layout.fillWidth: true
                        text: String(row.modelData.time_ms)
                              + (row.modelData.end_time_ms !== undefined ? " – " + row.modelData.end_time_ms : "") + " ms"
                        color: "#89929a"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: String(row.modelData.text || "")
                        color: row.index === root.activeIndex ? "#f7f4ed" : "#a7adb3"
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                }
                TapHandler { onTapped: root.seekRequested(Number(row.modelData.time_ms)) }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
            onCurrentIndexChanged: {
                if (currentIndex >= 0)
                    positionViewAtIndex(currentIndex, ListView.Contain)
                else
                    positionViewAtBeginning()
            }
            Label {
                anchors.fill: parent
                visible: root.lines.length === 0
                text: root.emptyText
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: "#89929a"
            }
        }
    }

    function findActiveIndex() {
        let low = 0
        let high = root.lines.length
        while (low < high) {
            const mid = Math.floor((low + high) / 2)
            if (Number(root.lines[mid].time_ms) <= root.position)
                low = mid + 1
            else
                high = mid
        }
        return low - 1
    }
}
