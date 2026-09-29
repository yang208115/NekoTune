import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    property var settings: ({})
    property bool connected: false
    property bool saving: false
    property string resultText: ""
    property bool failed: false
    signal saveRequested(var values)
    contentWidth: availableWidth
    clip: true

    function finishSaving(success, message) {
        saving = false
        failed = !success
        resultText = success ? i18n.text("settings_saved", i18n.language) : message
        if (success) apiKey.text = ""
    }
    onConnectedChanged: {
        if (!connected && saving) finishSaving(false, i18n.text("offline", i18n.language))
    }

    ColumnLayout {
        width: Math.min(root.availableWidth - 64, 720)
        x: 32
        y: 32
        spacing: 18

        Label {
            text: i18n.text("settings", i18n.language)
            color: "#f6f3fa"
            font.pixelSize: 28
            font.weight: Font.Bold
        }
        Label {
            Layout.fillWidth: true
            text: i18n.text("asr_settings_description", i18n.language)
            color: "#a59caf"
            wrapMode: Text.Wrap
            font.pixelSize: 13
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#30293c" }
        Label { text: "Aliyun DashScope · ASR"; color: "#cbb8ff"; font.pixelSize: 17; font.weight: Font.DemiBold }
        Label { text: "API Key"; color: "#d8d2e4"; font.pixelSize: 13 }
        TextField {
            id: apiKey
            objectName: "asrApiKey"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhHiddenText | Qt.ImhNoPredictiveText
            maximumLength: 4096
            enabled: root.connected && !root.saving
            placeholderText: i18n.text(root.settings.api_key_configured ? "asr_key_replace" : "asr_key_placeholder", i18n.language)
            color: "#f6f3fa"
            placeholderTextColor: "#837b92"
            leftPadding: 14
            background: Rectangle { color: "#191522"; radius: 8; border.color: apiKey.activeFocus ? "#cbb8ff" : "#393045" }
        }
        Label {
            Layout.fillWidth: true
            text: i18n.text(root.settings.api_key_configured ? "asr_key_configured" : "asr_key_missing", i18n.language)
            color: root.settings.api_key_configured ? "#7adfc6" : "#a59caf"
            font.pixelSize: 12
        }
        Label {
            Layout.fillWidth: true
            text: i18n.text("asr_upload_notice", i18n.language)
            color: "#a59caf"
            wrapMode: Text.Wrap
            font.pixelSize: 12
        }
        RowLayout {
            spacing: 12
            TextButton {
                objectName: "saveAsrSettings"
                text: i18n.text(root.saving ? "settings_saving" : "save", i18n.language)
                enabled: root.connected && !root.saving
                onClicked: {
                    const values = {}
                    if (apiKey.text.trim().length) values.api_key = apiKey.text.trim()
                    root.saving = true
                    root.resultText = ""
                    root.saveRequested(values)
                }
            }
            TextButton {
                objectName: "clearAsrKey"
                text: i18n.text("asr_clear_key", i18n.language)
                subtle: true
                enabled: root.connected && !root.saving && Boolean(root.settings.api_key_configured)
                onClicked: {
                    root.saving = true
                    root.resultText = ""
                    root.saveRequested({api_key: ""})
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.resultText
            visible: text.length > 0
            textFormat: Text.PlainText
            color: root.failed ? "#e8a9c3" : "#7adfc6"
            wrapMode: Text.Wrap
        }
        Item { Layout.preferredHeight: 32 }
    }
}
