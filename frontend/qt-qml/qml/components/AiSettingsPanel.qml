pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property var ai
    required property var translator
    property bool connected: true
    property string message: ""
    property bool failed: false
    readonly property var config: ai ? ai.configuration : ({})
    readonly property bool busy: ai ? ai.configBusy : false
    readonly property bool dirty: baseField.text.trim().replace(/\/+$/, "") !== String(config.base_url || "") || modelField.text.trim() !== String(config.model || "") || keyField.text.length > 0
    function t(key) {
        return translator.text(key, translator.language) || key;
    }
    function loadConfig() {
        baseField.text = String(config.base_url || "");
        modelField.text = String(config.model || "");
    }
    function save() {
        const data = {
            base_url: baseField.text.trim(),
            model: modelField.text.trim()
        };
        if (keyField.text.trim().length > 0)
            data.api_key = keyField.text.trim();
        keyField.text = "";
        message = "";
        ai.saveConfiguration(data);
    }
    Component.onCompleted: loadConfig()
    onVisibleChanged: {
        if (!visible)
            keyField.text = "";
        else if (ai && connected)
            ai.refreshConfiguration();
    }
    implicitHeight: content.implicitHeight + 36
    radius: 12
    color: "#211C2D"
    border.color: "#332C41"
    Connections {
        target: root.ai
        function onConfigurationChanged() {
            root.loadConfig();
        }
        function onRequestSucceeded(method) {
            if (method === "ai.config.get")
                return;
            root.failed = false;
            if (method === "ai.config.set")
                root.message = "ai_config_saved";
            else if (method === "ai.config.clear_key")
                root.message = "ai_key_cleared";
            else if (method === "ai.test")
                root.message = "ai_test_passed";
        }
        function onRequestFailed(method, reason) {
            root.failed = true;
            root.message = reason;
        }
    }
    ColumnLayout {
        id: content
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 18
        spacing: 10
        Label {
            text: root.t("ai_settings")
            color: "#F5F1FA"
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }
        Label {
            Layout.fillWidth: true
            text: root.t("ai_settings_help")
            wrapMode: Text.WordWrap
            color: "#AAA0B8"
            font.pixelSize: 12
        }
        Label {
            text: root.t("ai_base_url")
            color: "#D7CFE2"
        }
        InputField {
            id: baseField
            objectName: "aiBaseUrlField"
            Layout.fillWidth: true
            placeholderText: "https://api.example.com/v1"
            maximumLength: 2048
            enabled: root.connected && !root.busy
            Accessible.name: root.t("ai_base_url")
        }
        Label {
            text: root.t("ai_model")
            color: "#D7CFE2"
        }
        InputField {
            id: modelField
            objectName: "aiModelField"
            Layout.fillWidth: true
            placeholderText: root.t("ai_model_placeholder")
            maximumLength: 256
            enabled: root.connected && !root.busy
            Accessible.name: root.t("ai_model")
        }
        Label {
            text: root.t("ai_api_key")
            color: "#D7CFE2"
        }
        InputField {
            id: keyField
            objectName: "aiKeyField"
            Layout.fillWidth: true
            placeholderText: root.t("ai_key_placeholder")
            maximumLength: 4096
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            enabled: root.connected && !root.busy
            Accessible.name: root.t("ai_api_key")
        }
        Label {
            Layout.fillWidth: true
            text: root.t(baseField.text.trim().replace(/\/+$/, "") !== String(root.config.base_url || "") ? "ai_key_new_endpoint" : root.config.key_saved ? "ai_key_saved" : "ai_key_optional")
            wrapMode: Text.WordWrap
            color: "#AAA0B8"
            font.pixelSize: 12
        }
        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            spacing: 8
            TextButton {
                objectName: "aiSaveConfigButton"
                text: root.t("save")
                enabled: root.connected && !root.busy && baseField.text.trim().length > 0 && modelField.text.trim().length > 0
                onClicked: root.save()
            }
            TextButton {
                objectName: "aiTestButton"
                text: root.t("ai_test")
                subtle: true
                enabled: root.connected && !root.busy && Boolean(root.config.configured) && !root.dirty
                onClicked: {
                    root.message = "";
                    root.ai.testConnection();
                }
            }
            TextButton {
                objectName: "aiClearKeyButton"
                text: root.t("ai_clear_key")
                subtle: true
                visible: Boolean(root.config.key_saved)
                enabled: root.connected && !root.busy && !root.dirty
                onClicked: {
                    keyField.text = "";
                    root.message = "";
                    root.ai.clearKey();
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: root.busy || root.dirty || root.message.length > 0 || Boolean(root.config.credential_error)
            text: root.t(root.busy ? "ai_working" : root.message || root.config.credential_error || (root.dirty ? "ai_save_before_test" : ""))
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            color: root.failed || Boolean(root.config.credential_error) ? "#F0A4A4" : "#AAA0B8"
            font.pixelSize: 12
        }
    }
}
