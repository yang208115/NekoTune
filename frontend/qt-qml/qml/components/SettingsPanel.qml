pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var client
    required property var translator
    property bool saving: false
    property string message: ""
    property bool failed: false
    readonly property var account: client.status.kugou || ({})

    function t(key) { return translator.text(key, translator.language) }

    onVisibleChanged: if (!visible) keyField.text = ""

    Connections {
        target: root.client
        function onRequestSucceeded(method) {
            if (method !== "kugou.save_key" && method !== "kugou.clear_key") return
            root.saving = false
            root.failed = false
            root.message = root.t(method === "kugou.save_key" ? "kugou_key_saved" : "kugou_key_cleared")
        }
        function onRequestFailed(method, reason) {
            if (method !== "kugou.save_key" && method !== "kugou.clear_key") return
            root.saving = false
            root.failed = true
            root.message = reason
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        Label {
            text: root.t("settings")
            color: "#F5F1FA"
            font.pixelSize: 25
            font.weight: Font.DemiBold
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: settingsColumn.implicitHeight + 28
            radius: 12
            color: "#211C2D"
            border.color: "#332C41"

            ColumnLayout {
                id: settingsColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 12

                Label {
                    text: root.t("kugou_music")
                    color: "#F5F1FA"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.account.key_saved ? root.t("kugou_key_status_saved")
                        : root.account.configured ? root.t("kugou_key_status_external")
                            : root.t("kugou_key_status_missing")
                    color: root.account.configured ? "#98D8BC" : "#D7CFE2"
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.t("kugou_key_help")
                    color: "#AAA0B8"
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: keyField
                        objectName: "kugouKeyField"
                        Layout.fillWidth: true
                        placeholderText: root.t("kugou_key_placeholder")
                        echoMode: TextInput.Password
                        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                        maximumLength: 4096
                        enabled: root.client.connected && !root.saving && !root.account.busy
                        onAccepted: saveButton.clicked()
                    }

                    Button {
                        id: saveButton
                        objectName: "kugouSaveKeyButton"
                        text: root.t("kugou_key_save")
                        enabled: keyField.text.trim().length > 0 && root.client.connected
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

                Button {
                    objectName: "kugouClearKeyButton"
                    visible: Boolean(root.account.key_saved)
                    text: root.t("kugou_key_clear")
                    enabled: root.client.connected && !root.saving && !root.account.busy
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
                    color: root.failed ? "#F0A4A4" : "#98D8BC"
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
