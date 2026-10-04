import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
Item {
    id: root
    objectName: "extensionsPanel"
    required property var client
    required property var translator
    property string selectedId: ""
    property string configTarget: ""
    property int configRequest: -1
    property int logRequest: -1
    property string localError: ""
    function t(key) { return translator.text(key, translator.language) }
    function manage(method, params) { localError = ""; client.request("extensions." + method, params || {}) }
    function options(slot) {
        const items = [{id: "", label: t("extension_default")}]
        const list = client.contributions(slot === "theme" ? "themes" : "slots")
        for (const item of list) if (slot === "theme" || item.slot === slot) items.push({id: item.id, label: client.label(item.title || item.id, translator.language)})
        return items
    }
    FileDialog { id: packagePicker; title: root.t("extension_install"); nameFilters: ["NekoTune (*.zip)"]; onAccepted: root.manage("install", {path: String(selectedFile), replace: true}) }
    FolderDialog { id: directoryPicker; title: root.t("extension_mount"); onAccepted: root.manage("install", {path: String(selectedFolder), development: true, replace: true}) }
    Connections {
        target: root.client
        function onCompleted(id, data, error) {
            if (error) return
            if (id === root.configRequest) { configText.text = JSON.stringify(data.config, null, 2); configDialog.open() }
            if (id === root.logRequest) { logText.text = data.logs.map(line => line.time + " [" + line.level + "] " + line.message).join("\n"); logDialog.open() }
        }
    }
    ScrollView {
        anchors.fill: parent; contentWidth: availableWidth; clip: true
        ColumnLayout {
            width: parent.width; spacing: 18
            Label { text: root.t("extensions"); color: Theme.textPrimary; font.pixelSize: Theme.fontTitle; font.bold: true }
            Label { Layout.fillWidth: true; text: root.t("extension_description"); color: Theme.textSecondary; wrapMode: Text.Wrap }
            Flow {
                Layout.fillWidth: true; spacing: 8
                TextButton { text: root.t("extension_install"); onClicked: packagePicker.open() }
                TextButton { text: root.t("extension_mount"); subtle: true; onClicked: directoryPicker.open() }
                TextButton { text: root.t("extension_restore"); subtle: true; onClicked: root.client.resetInterface() }
            }
            Label { Layout.fillWidth: true; visible: text !== ""; text: root.localError || root.client.error; color: Theme.statusError; wrapMode: Text.Wrap; textFormat: Text.PlainText }
            Label { text: root.t("extension_empty"); color: Theme.textMuted; visible: root.client.items.length === 0 }
            Repeater {
                model: root.client.items
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    Layout.fillWidth: true; implicitHeight: cardBody.implicitHeight + 28
                    color: Theme.bgSurface; radius: Theme.radiusMd; border.color: Theme.borderSubtle
                    ColumnLayout {
                        id: cardBody; anchors.fill: parent; anchors.margins: 14; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: card.modelData.name + "  " + card.modelData.version; color: Theme.textPrimary; font.bold: true; textFormat: Text.PlainText; elide: Text.ElideRight }
                            Label { text: root.t("extension_state_" + card.modelData.state); color: card.modelData.state === "running" ? Theme.statusSuccess : Theme.textMuted }
                        }
                        Label { Layout.fillWidth: true; text: card.modelData.id + (card.modelData.development ? " · " + root.t("extension_development") : ""); color: Theme.textMuted; textFormat: Text.PlainText }
                        Label { Layout.fillWidth: true; visible: text !== ""; text: card.modelData.error || card.modelData.description; color: card.modelData.error ? Theme.statusError : Theme.textSecondary; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                        Flow {
                            Layout.fillWidth: true; spacing: 8
                            TextButton {
                                text: root.t(card.modelData.state === "running" ? "extension_disable" : "extension_enable")
                                onClicked: {
                                    root.selectedId = card.modelData.id
                                    if (card.modelData.state === "running") root.manage("disable", {id: root.selectedId})
                                    else if (!card.modelData.trusted) trustDialog.open()
                                    else root.manage("enable", {id: root.selectedId})
                                }
                            }
                            TextButton { text: root.t("extension_reload"); subtle: true; enabled: card.modelData.state === "running"; onClicked: root.manage("reload", {id: card.modelData.id}) }
                            TextButton { text: root.t("extension_config"); subtle: true; onClicked: { root.configTarget = card.modelData.id; root.configRequest = root.client.request("extensions.get_config", {id: root.configTarget}) } }
                            TextButton { text: root.t("extension_logs"); subtle: true; onClicked: { root.selectedId = card.modelData.id; root.logRequest = root.client.request("extensions.logs", {id: root.selectedId}) } }
                            TextButton { text: root.t("extension_uninstall"); subtle: true; onClicked: { root.selectedId = card.modelData.id; clearData.checked = false; uninstallDialog.open() } }
                        }
                    }
                }
            }
            Label { text: root.t("extension_appearance"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle; Layout.topMargin: 10 }
            Repeater {
                model: {
                    const items = root.client.items
                    const slots = ["shell", "page:home", "page:library", "page:queue", "page:lyrics", "bottomPlayer", "theme"]
                    for (const item of root.client.contributions("slots"))
                        if (item.slot && !slots.includes(item.slot)) slots.push(item.slot)
                    return slots
                }
                delegate: RowLayout {
                    id: selector
                    required property string modelData
                    Layout.fillWidth: true
                    Label {
                        text: {
                            const key = "extension_slot_" + selector.modelData.replace(":", "_")
                            const translated = root.t(key)
                            return translated === key ? selector.modelData : translated
                        }
                        color: Theme.textSecondary; Layout.preferredWidth: 160; elide: Text.ElideRight
                    }
                    ChoiceField {
                        Layout.fillWidth: true
                        model: { const revision = root.client.selections; const items = root.client.items; return root.options(selector.modelData) }
                        textRole: "label"; valueRole: "id"
                        currentIndex: Math.max(0, model.findIndex(item => item.id === (root.client.selections[selector.modelData] || "")))
                        onActivated: root.manage("select", {slot: selector.modelData, contribution: currentValue})
                    }
                }
            }
        }
    }
    Dialog {
        id: trustDialog; title: root.t("extension_trust_title"); modal: true; anchors.centerIn: Overlay.overlay; parent: Overlay.overlay; width: 500
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: root.t("extension_trust_body"); color: Theme.textPrimary; wrapMode: Text.Wrap }
        onAccepted: root.manage("enable", {id: root.selectedId, trusted: true})
    }
    Dialog {
        id: uninstallDialog; title: root.t("extension_uninstall"); modal: true; anchors.centerIn: Overlay.overlay; parent: Overlay.overlay; width: 460
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: CheckBox { id: clearData; text: root.t("extension_clear_data") }
        onAccepted: root.manage("uninstall", {id: root.selectedId, clearData: clearData.checked})
    }
    Dialog {
        id: configDialog; title: root.t("extension_config"); modal: true; anchors.centerIn: Overlay.overlay; parent: Overlay.overlay; width: 620; height: 450
        standardButtons: Dialog.Save | Dialog.Cancel
        contentItem: ScrollView { TextArea { id: configText; textFormat: TextEdit.PlainText; font.family: Theme.fontFamilyMonospace; selectByMouse: true; wrapMode: TextEdit.Wrap } }
        onAccepted: { try { root.manage("set_config", {id: root.configTarget, config: JSON.parse(configText.text)}) } catch(error) { root.localError = String(error) } }
    }
    Dialog {
        id: logDialog; title: root.t("extension_logs"); modal: true; anchors.centerIn: Overlay.overlay; parent: Overlay.overlay; width: 720; height: 480
        standardButtons: Dialog.Close
        contentItem: ScrollView { TextArea { id: logText; readOnly: true; textFormat: TextEdit.PlainText; font.family: Theme.fontFamilyMonospace; selectByMouse: true; wrapMode: TextEdit.Wrap } }
    }
}
