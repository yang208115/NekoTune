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
    property bool saving: false
    property string message: ""
    property bool failed: false
    property var account: ({})
    property bool connected: true
    property string databasePath: ""
    property bool showDataLocation: false

    property string savedWorkerUrl: ""
    property bool savedEnabled: false
    readonly property bool configDirty: enableSwitch.checked !== savedEnabled
        || workerField.text.trim().replace(/\/+$/, "") !== savedWorkerUrl

    function syncConfiguration() {
        const url = String(account.worker_url || "")
        const enabled = Boolean(account.enabled)
        if (url !== savedWorkerUrl || enabled !== savedEnabled) {
            savedWorkerUrl = url
            savedEnabled = enabled
            workerField.text = url
            enableSwitch.checked = enabled
        }
    }
    onAccountChanged: syncConfiguration()
    Component.onCompleted: syncConfiguration()

    function t(key) { return translator.text(key, translator.language) }

    onVisibleChanged: if (!visible) keyField.text = ""

    Connections {
        target: root.client
        function onRequestSucceeded(method) {
            if (method !== "kugou.save_key" && method !== "kugou.clear_key" && method !== "kugou.config.set") return
            root.saving = false
            root.failed = false
            root.message = root.t(method === "kugou.config.set" ? "kugou_config_saved" : method === "kugou.save_key" ? "kugou_key_saved" : "kugou_key_cleared")
        }
        function onRequestFailed(method, reason) {
            if (method !== "kugou.save_key" && method !== "kugou.clear_key" && method !== "kugou.config.set") return
            root.saving = false
            root.failed = true
            root.message = reason
        }
    }

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

        AiSettingsPanel {
            Layout.fillWidth: true
            visible: root.ai !== null
            ai: root.ai
            translator: root.translator
            connected: root.connected
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: settingsColumn.implicitHeight + 28
            radius: Theme.radiusMd
            color: Theme.bgRaised
            border.color: Theme.borderSubtle

            ColumnLayout {
                id: settingsColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 12

                Label {
                    text: root.t("kugou_music")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontDialogTitle
                    font.weight: Font.DemiBold
                }

                Switch {
                    id: enableSwitch
                    objectName: "kugouEnabledSwitch"
                    text: root.t("kugou_enable")
                    enabled: root.connected && !root.saving && !root.account.busy
                    contentItem: Label {
                        text: enableSwitch.text
                        color: Theme.textPrimary
                        opacity: enableSwitch.enabled ? 1 : 0.5
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: enableSwitch.indicator.width + enableSwitch.spacing
                    }
                    indicator: Rectangle {
                        implicitWidth: 40
                        implicitHeight: 22
                        x: enableSwitch.leftPadding
                        y: (enableSwitch.height - height) / 2
                        radius: 11
                        color: enableSwitch.checked ? Theme.accentPrimary : Theme.bgSurface
                        border.color: enableSwitch.activeFocus ? Theme.accentPrimary : Theme.borderControl
                        opacity: enableSwitch.enabled ? 1 : 0.5
                        Rectangle {
                            x: enableSwitch.checked ? parent.width - width - 3 : 3
                            y: 3
                            width: 16; height: 16; radius: 8
                            color: Theme.textPrimary
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.t("kugou_worker_help")
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                }

                Label { text: root.t("kugou_worker_url"); color: Theme.textSecondary }
                InputField {
                    id: workerField
                    objectName: "kugouWorkerUrlField"
                    Layout.fillWidth: true
                    placeholderText: "https://your-worker.example.com"
                    Accessible.name: root.t("kugou_worker_url")
                    maximumLength: 2048
                    enabled: root.connected && !root.saving && !root.account.busy
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                }

                TextButton {
                    objectName: "kugouSaveConfigButton"
                    text: root.t("kugou_config_save")
                    enabled: root.configDirty && root.connected && !root.saving && !root.account.busy
                             && (!enableSwitch.checked || workerField.text.trim().length > 0)
                    onClicked: {
                        root.saving = true
                        root.message = ""
                        root.client.kugouSaveConfiguration(enableSwitch.checked, workerField.text.trim())
                    }
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.account.key_saved ? root.t("kugou_key_status_saved")
                        : root.account.configured ? root.t("kugou_key_status_external")
                            : root.t("kugou_key_status_missing")
                    color: root.account.configured ? Theme.statusSuccess : Theme.textSecondary
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.t("kugou_key_help")
                    color: Theme.textMuted
                }

                Label {
                    Layout.fillWidth: true
                    visible: Boolean(root.account.credential_error)
                    text: root.t("kugou_keyring_error")
                    color: Theme.statusError
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    InputField {
                        id: keyField
                        objectName: "kugouKeyField"
                        Layout.fillWidth: true
                        placeholderText: root.t("kugou_key_placeholder")
                        echoMode: TextInput.Password
                        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                        maximumLength: 4096
                        enabled: root.connected && !root.saving && !root.account.busy
                        onAccepted: saveButton.clicked()
                    }

                    TextButton {
                        id: saveButton
                        objectName: "kugouSaveKeyButton"
                        text: root.t("kugou_key_save")
                        enabled: keyField.text.trim().length > 0 && root.connected
                                 && !root.saving && !root.account.busy
                        onClicked: {
                            const key = keyField.text.trim()
                            keyField.text = ""
                            root.saving = true
                            root.message = ""
                            root.client.kugouSaveKey(key)
                        }
                    }
                }

                TextButton {
                    objectName: "kugouClearKeyButton"
                    visible: Boolean(root.account.key_saved)
                    text: root.t("kugou_key_clear")
                    enabled: root.connected && !root.saving && !root.account.busy
                    onClicked: {
                        keyField.text = ""
                        root.saving = true
                        root.message = ""
                        root.client.kugouClearKey()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.message.length > 0
                    text: root.message
                    wrapMode: Text.WordWrap
                    color: root.failed ? Theme.statusError : Theme.statusSuccess
                }
            }
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
