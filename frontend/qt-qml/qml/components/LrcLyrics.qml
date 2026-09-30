pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: root

    property var lines: []
    property string plainText: ""
    property real position: 0
    property int activeIndex: -1
    readonly property real activeTime: activeIndex >= 0 ? Number(lines[activeIndex].time_ms) : -1
    property color activeColor: "#f6f3fa"
    property color inactiveColor: "#645e73"

    property bool userScrolling: false

    signal seekRequested(real positionMs)

    clip: true

    Timer {
        id: resumeAutoScrollTimer
        interval: 3500
        repeat: false
        onTriggered: {
            root.userScrolling = false
            root.scrollToActive()
        }
    }

    onLinesChanged: {
        updateActiveLine()
        if (activeIndex < 0) lyricsList.positionViewAtBeginning()
    }
    onPositionChanged: updateActiveLine()
    onActiveIndexChanged: {
        if (!root.userScrolling) {
            Qt.callLater(scrollToActive)
        }
    }

    ListView {
        id: lyricsList
        anchors.fill: parent
        anchors.topMargin: 20
        anchors.bottomMargin: 20
        clip: true
        spacing: 14
        visible: root.lines && root.lines.length > 0
        model: root.lines
        currentIndex: root.activeIndex
        boundsBehavior: Flickable.DragOverBounds

        onMovingChanged: {
            if (moving) {
                root.userScrolling = true
                resumeAutoScrollTimer.restart()
            }
        }
        onFlickingChanged: {
            if (flicking) {
                root.userScrolling = true
                resumeAutoScrollTimer.restart()
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            width: 4
            contentItem: Rectangle { radius: 2; color: "#453e56" }
        }

        delegate: Item {
            id: lineDelegate
            required property var modelData
            required property int index

            objectName: "lyricLine" + index
            readonly property bool active: index === root.activeIndex
            readonly property real timeMs: Number(modelData.time_ms || 0)

            width: lyricsList.width
            height: Math.max(34, lineText.implicitHeight + 10)

            Rectangle {
                anchors.fill: parent
                radius: 8
                color: lineMouse.hovered ? "#1c1828" : "transparent"
            }

            Text {
                id: lineText
                anchors.centerIn: parent
                width: parent.width - 24
                objectName: "lyricText" + lineDelegate.index
                text: lineDelegate.modelData.text || "♪"
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: lineDelegate.active ? 20 : 15
                font.weight: lineDelegate.active ? Font.Bold : Font.Normal
                color: lineDelegate.active ? root.activeColor : lineMouse.hovered ? "#d0c9dc" : root.inactiveColor
                opacity: lineDelegate.active ? 1.0 : lineMouse.hovered ? 0.9 : 0.6

                Behavior on font.pixelSize {
                    NumberAnimation { duration: 140; easing.type: Easing.OutQuad }
                }
                Behavior on opacity {
                    NumberAnimation { duration: 140 }
                }
                Behavior on color {
                    ColorAnimation { duration: 140 }
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
            GradientStop { position: 0.0; color: "#110f17" }
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
            GradientStop { position: 1.0; color: "#110f17" }
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
            color: "#d8d1e4"
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
        let low = 0
        let high = root.lines.length
        while (low < high) {
            const mid = Math.floor((low + high) / 2)
            if (Number(root.lines[mid].time_ms) <= root.position)
                low = mid + 1
            else
                high = mid
        }
        root.activeIndex = low - 1
    }

    function scrollToActive() {
        if (!root.userScrolling && root.activeIndex >= 0 && root.activeIndex < lyricsList.count) {
            lyricsList.positionViewAtIndex(root.activeIndex, ListView.Center)
        }
    }
}
