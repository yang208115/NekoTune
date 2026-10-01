import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: row
    required property var song
    property int rowIndex: 0
    property bool selected: false
    property bool current: false
    property bool playing: false
    property bool available: true
    property bool connected: true
    property bool compact: false
    property bool downloadRow: false
    property bool downloadEnabled: false
    signal downloadRequested()
    property bool showCheckbox: false
    property string playObjectName: ""
    property string checkboxObjectName: ""
    property var menuActions: []
    signal selectedRequested(int modifiers)
    signal checkboxRequested()
    signal activated()
    signal playClicked()
    signal actionRequested(string action)
    height: 64
    radius: 8
    color: selected ? "#322743" : hover.hovered ? "#2A2338" : "transparent"
    border.color: activeFocus ? "#CBB8FF" : "transparent"
    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: String(song.title || i18n.text("untitled", i18n.language)) + " " + String(song.artist || "")
    Accessible.selected: selected
    Keys.onReturnPressed: if (!downloadRow && available && connected) activated()
    Keys.onEnterPressed: if (!downloadRow && available && connected) activated()
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered || activeFocus
    ToolTip.text: String(song.title || "") + (song.artist ? " — " + song.artist : "")
    ToolTip.delay: 800
    function openMenu(anchor, px, py) { contextMenu.openAt(anchor, px, py) }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: mouse => {
            row.forceActiveFocus()
            if (mouse.button === Qt.RightButton && row.menuActions.length) {
                if (!row.selected) row.selectedRequested(0)
                row.openMenu(row, mouse.x, mouse.y)
            } else row.selectedRequested(mouse.modifiers)
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton && !row.downloadRow && row.available && row.connected) row.activated()
        }
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 12
        CheckBox {
            id: selectionBox
            visible: row.showCheckbox
            checked: row.selected
            objectName: row.checkboxObjectName
            Accessible.name: i18n.text("select_song", i18n.language).replace("%1", String(row.song.title || ""))
            Layout.preferredWidth: 28
            onClicked: row.checkboxRequested()
            indicator: Rectangle {
                width: 18; height: 18; radius: 4
                anchors.centerIn: parent
                color: selectionBox.checked ? "#CBB8FF" : "#17141F"
                border.color: "#8D809F"
                Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 2; color: "#21172F"; visible: selectionBox.checked }
            }
            contentItem: Item {}
        }
        Item {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 40
            Label {
                anchors.centerIn: parent
                text: String(row.rowIndex + 1)
                visible: row.downloadRow || (!hover.hovered && !row.selected && !row.current && !row.activeFocus)
                color: "#AAA0B8"
                font.pixelSize: 12
            }
            IconButton {
                anchors.centerIn: parent
                objectName: row.playObjectName
                implicitWidth: 32; implicitHeight: 36
                kind: row.current && row.playing ? "pause" : "play"
                glyphColor: row.current ? "#CBB8FF" : "#F5F1FA"
                visible: !row.downloadRow && (hover.hovered || row.selected || row.current || row.activeFocus)
                enabled: row.available && row.connected
                tooltipText: i18n.text(row.current && row.playing ? "pause" : "play", i18n.language)
                onClicked: row.playClicked()
            }
        }
        Item {
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            visible: !row.compact
            Image { anchors.fill: parent; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop; mipmap: true }
            Image { objectName: "trackCover"; anchors.fill: parent; source: String(row.song.cover_url || ""); fillMode: Image.PreserveAspectCrop; asynchronous: true; visible: status === Image.Ready }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: row.song.title || i18n.text("untitled", i18n.language)
                textFormat: Text.PlainText
                color: row.current ? "#CBB8FF" : row.available ? "#F5F1FA" : "#AAA0B8"
                font.pixelSize: 14
                font.weight: row.current ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: row.available ? String(row.song.artist || String(row.song.path || "").split("/").pop() || "")
                                    : i18n.text("file_unavailable", i18n.language)
                textFormat: Text.PlainText
                color: row.available ? "#AAA0B8" : "#E8C58A"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }
        Label {
            text: {
                const duration = Number(row.song.duration || row.song.duration_ms || 0)
                if (duration <= 0) return "--:--"
                const seconds = Math.floor(duration / 1000)
                return Math.floor(seconds / 60) + ":" + ("0" + seconds % 60).slice(-2)
            }
            color: "#AAA0B8"
            font.pixelSize: 12
            Layout.preferredWidth: 44
            horizontalAlignment: Text.AlignRight
        }
        TextButton { text: i18n.text("kugou_download", i18n.language); visible: row.downloadRow; enabled: row.downloadEnabled; subtle: true; onClicked: row.downloadRequested() }
        IconButton {
            visible: row.menuActions.length > 0
            id: more
            kind: "more"
            implicitWidth: 32; implicitHeight: 36
            tooltipText: i18n.text("track_actions", i18n.language)
            onClicked: row.openMenu(more)
        }
    }
    ActionMenu { id: contextMenu; actions: row.menuActions; onChosen: action => row.actionRequested(action) }
}
