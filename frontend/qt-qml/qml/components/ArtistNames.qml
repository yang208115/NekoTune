pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    property string artist: ""
    property string fallbackText: ""
    property color color: "#AAA0B8"
    property font font: Qt.font({pixelSize: 12})
    property bool summarizeOverflow: false
    property string remainingTextTemplate: "+%1"
    readonly property var names: {
        const seen = new Set()
        return artist.split(/[,，、;；\r\n]+/).map(name => name.trim()).filter(name => {
            const key = name.toLocaleLowerCase()
            if (!name || seen.has(key)) return false
            seen.add(key)
            return true
        })
    }
    spacing: 6
    readonly property int visibleNameCount: {
        if (!root.summarizeOverflow) return root.names.length
        // Font changes must recalculate the fit even when width stays the same.
        fontMetrics.font
        for (let count = root.names.length; count > 0; --count) {
            const textWidth = Math.ceil(fontMetrics.advanceWidth(root.names.slice(0, count).join(" · ")))
            const remaining = root.names.length - count
            const extraWidth = remaining > 0 ? root.spacing
                + Math.ceil(fontMetrics.advanceWidth(root.remainingTextTemplate.replace("%1", remaining))) : 0
            if (textWidth + extraWidth <= root.width) return count
        }
        return Math.min(1, root.names.length)
    }
    readonly property int remainingCount: root.names.length - root.visibleNameCount
    FontMetrics { id: fontMetrics; font: root.font }
    clip: true
    Accessible.role: Accessible.StaticText
    Accessible.name: names.length ? names.join(", ") : fallbackText
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered && root.names.length > 0
    ToolTip.text: root.names.join(" · ")
    ToolTip.delay: 800

    Repeater {
        model: root.summarizeOverflow ? [] : root.names
        delegate: RowLayout {
            id: entry
            required property string modelData
            required property int index
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: implicitWidth
            spacing: root.spacing
            Label {
                objectName: "artistName" + entry.index
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: entry.modelData
                textFormat: Text.PlainText
                color: root.color
                font: root.font
                elide: Text.ElideRight
            }
            Label {
                visible: entry.index < root.names.length - 1
                text: "·"
                color: root.color
                font: root.font
            }
        }
    }
    Item {
        id: summary
        visible: root.summarizeOverflow && root.names.length > 0
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitWidth: summaryName.implicitWidth
            + (remaining.visible ? root.spacing + remaining.implicitWidth : 0)
        implicitHeight: Math.max(summaryName.implicitHeight, remaining.implicitHeight)
        Label {
            id: summaryName
            objectName: "artistSummary"
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(Math.ceil(implicitWidth), Math.max(0, summary.width
                - (remaining.visible ? root.spacing + Math.ceil(remaining.implicitWidth) : 0)))
            text: root.names.slice(0, root.visibleNameCount).join(" · ")
            textFormat: Text.PlainText
            color: root.color
            font: root.font
            elide: Text.ElideRight
        }
        Label {
            id: remaining
            objectName: "artistRemainingCount"
            anchors.left: summaryName.right
            anchors.leftMargin: root.spacing
            anchors.verticalCenter: parent.verticalCenter
            visible: root.remainingCount > 0
            text: root.remainingTextTemplate.replace("%1", root.remainingCount)
            textFormat: Text.PlainText
            color: root.color
            font: root.font
        }
    }
    Label {
        visible: root.names.length === 0
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: root.fallbackText
        textFormat: Text.PlainText
        color: root.color
        font: root.font
        elide: Text.ElideRight
    }
    Item { visible: !root.summarizeOverflow; Layout.fillWidth: true; Layout.minimumWidth: 0 }
}
