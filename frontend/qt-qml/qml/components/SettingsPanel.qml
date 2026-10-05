pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Settings displays account capability flags rather than stored credentials.
// The password input is write-only and cleared on hiding/submission.
// Save/clear failures settle their own busy state for explicit retry.
// Language changes use the shared translator and merged settings persistence.
// The AI panel uses its own endpoint-scoped key configuration lifecycle.
Item {
    id: root
    required property var client
    required property var translator
    property var ai: null
    property var audioOutput: null
    property bool saving: false
    property string message: ""
    property bool failed: false
    property var account: ({})
    property bool connected: true
    property string databasePath: ""
    property bool showDataLocation: false
    property var extensionSettings: []
    signal extensionSettingsRequested(string id)

    function t(key) { return translator.text(key, translator.language) }

    ScrollView {
        id: settingsScroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
    ColumnLayout {
        width: settingsScroll.availableWidth
        spacing: 14

        Label {
            text: root.t("settings")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 112
            radius: Theme.radiusMd; color: Theme.bgSurface; border.color: Theme.borderSubtle
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 12
                Label { text: root.t("general_settings"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
                RowLayout {
                    Label { Layout.fillWidth: true; text: root.t("language"); color: Theme.textSecondary; font.pixelSize: Theme.fontBody }
                    TextButton { text: "中文"; subtle: root.translator.language !== "zh"; onClicked: root.translator.language = "zh" }
                    TextButton { text: "English"; subtle: root.translator.language !== "en"; onClicked: root.translator.language = "en" }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.audioOutput !== null
            implicitHeight: outputSettings.implicitHeight + 36
            radius: Theme.radiusMd; color: Theme.bgSurface; border.color: Theme.borderSubtle
            ColumnLayout {
                id: outputSettings
                anchors.fill: parent; anchors.margins: 18; spacing: 12
                Label { text: root.t("audio_output"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
                AudioOutputPicker {
                    objectName: "settingsAudioOutputPicker"
                    Layout.fillWidth: true
                    controller: root.audioOutput; translator: root.translator; connected: root.connected
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.extensionSettings.length > 0
            spacing: 8
            Label { text: root.t("extension_settings"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
            Repeater {
                model: root.extensionSettings
                delegate: TextButton {
                    id: entry
                    required property var modelData
                    objectName: "settings_entry_" + modelData.id
                    Layout.fillWidth: true
                    text: modelData.title
                    subtle: true
                    contentItem: RowLayout {
                        Label { Layout.fillWidth: true; text: entry.text; color: Theme.textSecondary; textFormat: Text.PlainText; elide: Text.ElideRight }
                        Label { text: "›"; color: Theme.textMuted }
                    }
                    onClicked: root.extensionSettingsRequested(modelData.id)
                }
            }
        }

        AiSettingsPanel {
            Layout.fillWidth: true
            visible: root.ai !== null
            ai: root.ai
            translator: root.translator
            connected: root.connected
        }

        TextButton { visible: Boolean(root.databasePath); text: root.t("database"); subtle: true; onClicked: root.showDataLocation = !root.showDataLocation }
        TextArea {
            Layout.fillWidth: true
            visible: root.showDataLocation && Boolean(root.databasePath)
            text: root.databasePath
            readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
            color: Theme.textMuted; font.pixelSize: Theme.fontCaption
            background: Rectangle { color: Theme.bgSurface; radius: Theme.radiusSm }
            Accessible.name: root.t("database")
        }
    }
    }
}
