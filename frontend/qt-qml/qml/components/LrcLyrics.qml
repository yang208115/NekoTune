import QtQuick
import QtQuick.Controls

Item {
    id: root

    property var lines: []
    property string plainText: ""
    property real position: 0
    property int activeIndex: -1
    readonly property real activeTime: activeIndex >= 0 ? Number(lines[activeIndex].time_ms) : -1
    property color activeColor: "#f7f4ed"
    property color inactiveColor: "#89929a"

    clip: true
    onLinesChanged: {
        updateActiveLine()
        if (activeIndex < 0) lyricsList.positionViewAtBeginning()
    }
    onPositionChanged: updateActiveLine()
    onActiveIndexChanged: Qt.callLater(scrollToActive)

    ListView {
        id: lyricsList
        anchors.fill: parent
        clip: true
        spacing: 8
        visible: root.lines.length > 0
        model: root.lines
        currentIndex: root.activeIndex
        onCountChanged: Qt.callLater(root.scrollToActive)
        ScrollBar.vertical: ScrollBar {}

        delegate: Text {
            required property var modelData
            readonly property bool active: Number(modelData.time_ms) === root.activeTime
            width: lyricsList.width - 12
            height: Math.max(22, implicitHeight)
            text: modelData.text
            textFormat: Text.PlainText
            color: active ? root.activeColor : root.inactiveColor
            opacity: active ? 1.0 : 0.72
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: active ? 18 : 15
            font.weight: active ? Font.Bold : Font.Normal
        }
    }

    ScrollView {
        anchors.fill: parent
        visible: root.lines.length === 0
        contentWidth: availableWidth
        TextArea {
            text: root.plainText
            textFormat: TextEdit.PlainText
            readOnly: true
            wrapMode: TextEdit.Wrap
            color: root.activeColor
            font.pixelSize: 15
            background: null
        }
    }

    function updateActiveLine() {
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
        if (root.activeIndex >= 0 && root.activeIndex < lyricsList.count)
            lyricsList.positionViewAtIndex(root.activeIndex, ListView.Center)
    }
}
