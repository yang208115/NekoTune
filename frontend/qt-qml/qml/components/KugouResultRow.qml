pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: row
    required property var song
    required property var translator
    property int rowIndex: 0
    property bool selected: false
    property bool compact: width < 900
    property real artistWidth: Math.min(280, width * 0.21)
    property real albumWidth: Math.min(320, width * 0.24)
    property bool downloadEnabled: false
    signal selectedRequested()
    signal downloadRequested()
    function t(key) { return translator.text(key, translator.language) }
    height: 72
    radius: 10
    color: selected ? palette.bgSelected : hover.hovered ? palette.bgHover
                                                        : rowIndex % 2 ? "#19151F" : palette.bgSurface
    border.color: activeFocus ? palette.accentPrimary : selected ? "#66547F" : "transparent"
    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: String(song.title || t("untitled")) + ", " + String(song.artist || "")
    Accessible.selected: selected
    Theme { id: palette }
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered
    ToolTip.text: String(song.title || "") + "\n" + String(song.artist || "")
                 + (song.album ? "\n" + song.album : "")
    ToolTip.delay: 800
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: { row.forceActiveFocus(); row.selectedRequested() }
    }
    Rectangle {
        x: 0; width: 3; height: 24; radius: 1.5
        anchors.verticalCenter: parent.verticalCenter
        color: palette.accentPrimary
        visible: row.selected
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16; anchors.rightMargin: 16
        spacing: 14
        Label {
            Layout.preferredWidth: 28
            text: String(row.rowIndex + 1).padStart(2, "0")
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 12
            color: row.selected ? palette.accentPrimary : palette.textMuted
        }
        Rectangle {
            Layout.preferredWidth: 48; Layout.preferredHeight: 48
            radius: 8
            color: palette.bgRaised
            border.color: palette.borderSubtle
            Image { anchors.fill: parent; anchors.margins: 3; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop; mipmap: true }
            Image {
                objectName: "trackCover"
                anchors.fill: parent; anchors.margins: 3
                source: String(row.song.cover_url || "")
                fillMode: Image.PreserveAspectCrop
                asynchronous: true; mipmap: true
                visible: status === Image.Ready
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            spacing: 5
            Label {
                objectName: "kugouResultTitle"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                text: row.song.title || row.t("untitled")
                textFormat: Text.PlainText
                color: row.selected ? palette.accentPrimary : palette.textPrimary
                font.pixelSize: 14; font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            ArtistNames {
                objectName: "kugouCompactArtists"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                visible: row.compact
                artist: String(row.song.artist || "")
                fallbackText: row.t("unknown_artist")
            }
        }
        ArtistNames {
            objectName: "kugouResultArtists"
            Layout.preferredWidth: row.artistWidth
            Layout.fillWidth: false
            Layout.minimumWidth: 0
            visible: !row.compact
            artist: String(row.song.artist || "")
            fallbackText: row.t("unknown_artist")
            color: palette.textSecondary
            font.pixelSize: 13
        }
        Label {
            objectName: "kugouResultAlbum"
            Layout.preferredWidth: row.albumWidth
            Layout.minimumWidth: 0
            visible: !row.compact
            text: row.song.album || "—"
            textFormat: Text.PlainText
            color: palette.textMuted; font.pixelSize: 13
            elide: Text.ElideRight
        }
        Label {
            objectName: "trackDuration"
            Layout.preferredWidth: 48
            text: {
                const duration = Number(row.song.duration_ms || row.song.duration || 0)
                if (duration <= 0) return "--:--"
                const seconds = Math.floor(duration / 1000)
                return Math.floor(seconds / 60) + ":" + ("0" + seconds % 60).slice(-2)
            }
            horizontalAlignment: Text.AlignRight
            color: palette.textMuted; font.pixelSize: 12
        }
        TextButton {
            objectName: "kugouDownloadButton"
            Layout.preferredWidth: 124
            implicitHeight: 36
            text: row.t("kugou_download")
            enabled: row.downloadEnabled
            subtle: true
            subtleBg: row.selected ? "#40324F" : "#211C2D"
            subtleBorder: row.selected ? "#8D72AF" : palette.borderSubtle
            subtleText: palette.accentPrimary
            onClicked: row.downloadRequested()
        }
    }
}
