pragma ComponentBehavior: Bound
import QtQuick
import "qrc:/qml/components"
import QtQuick.Controls
import QtQuick.Layouts

// Search results are remote provider records, not local playback queue entries.
// The row offers selection and an explicit download action but no implicit playback.
// The containing panel supplies account/download admission through downloadEnabled.
Rectangle {
    id: row
    required property var song
    required property var translator
    property int rowIndex: 0
    property bool selected: false
    // Wide artist/album widths are supplied consistently with the panel's column header.
    // Compact layout hides those columns and shows artist metadata below the title instead.
    // The title remains flexible so long provider text cannot displace the download button.
    property bool compact: width < 900
    property real artistWidth: Math.min(280, width * 0.21)
    property real albumWidth: Math.min(320, width * 0.24)
    property bool downloadEnabled: false
    signal selectedRequested()
    signal downloadRequested()
    signal playRequested()
    function t(key) { return translator.text(key, translator.language) }
    height: 72
    radius: 10
    color: selected ? Theme.bgSelected : hover.hovered ? Theme.bgHover
                                                        : rowIndex % 2 ? Theme.bgRaised : Theme.bgSurface
    border.color: activeFocus ? Theme.accentPrimary : selected ? Theme.borderControl : "transparent"
    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: String(song.title || t("untitled")) + ", " + String(song.artist || "")
    Accessible.selected: selected
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered
    ToolTip.text: String(song.title || "") + "\n" + String(song.artist || "")
                 + (song.album ? "\n" + song.album : "")
    ToolTip.delay: 800
    // The row body only selects and focuses this result.
    // The later child button owns its click, so downloading does not also change row selection.
    // Enter is not bound to an automatic download or playback command here.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: { row.forceActiveFocus(); row.selectedRequested() }
    }
    Rectangle {
        x: 0; width: 3; height: 24; radius: 1.5
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.accentPrimary
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
            color: row.selected ? Theme.accentPrimary : Theme.textMuted
        }
        Rectangle {
            Layout.preferredWidth: 48; Layout.preferredHeight: 48
            radius: 8
            color: Theme.bgRaised
            border.color: Theme.borderSubtle
            Image { anchors.fill: parent; anchors.margins: 3; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop; mipmap: true }
            Image {
                // Keep the default artwork underneath throughout asynchronous loading or failure.
                // Only a successfully decoded image becomes visible above that fallback.
                // A nonempty remote URL alone is not evidence that its artwork is usable.
                objectName: "trackCover"
                anchors.fill: parent; anchors.margins: 3
                source: String(row.song.cover_url || "")
                fillMode: Image.PreserveAspectCrop
                asynchronous: true; mipmap: true
                visible: status === Image.Ready
            }
        }
        ColumnLayout {
            // Allow this flexible column to shrink below its text's implicit width.
            // Without a zero minimum, RowLayout can let a long title push fixed action columns outside.
            // Elision then describes the available space rather than changing the row's width contract.
            Layout.fillWidth: true; Layout.minimumWidth: 0
            spacing: 5
            Label {
                objectName: "kugouResultTitle"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                text: row.song.title || row.t("untitled")
                textFormat: Text.PlainText
                color: row.selected ? Theme.accentPrimary : Theme.textPrimary
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
            color: Theme.textSecondary
            font.pixelSize: 13
        }
        Label {
            objectName: "kugouResultAlbum"
            Layout.preferredWidth: row.albumWidth
            Layout.minimumWidth: 0
            visible: !row.compact
            text: row.song.album || "—"
            textFormat: Text.PlainText
            color: Theme.textMuted; font.pixelSize: 13
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
            color: Theme.textMuted; font.pixelSize: 12
        }
        TextButton {
            objectName: "kugouPlayButton"
            Layout.preferredWidth: 48; implicitHeight: 36; text: "▶"
            enabled: row.downloadEnabled; subtle: true
            Accessible.name: row.t("extension_play")
            onClicked: row.playRequested()
        }
        TextButton {
            objectName: "kugouDownloadButton"
            Layout.preferredWidth: 124
            implicitHeight: 36
            text: row.t("kugou_download")
            enabled: row.downloadEnabled
            subtle: true
            subtleBg: row.selected ? Theme.bgSelected : Theme.bgRaised
            subtleBorder: row.selected ? Theme.borderControl : Theme.borderSubtle
            subtleText: Theme.accentPrimary
            onClicked: row.downloadRequested()
        }
    }
}
