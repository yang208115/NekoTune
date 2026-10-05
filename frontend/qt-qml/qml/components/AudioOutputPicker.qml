pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var controller
    required property var translator
    property bool connected: true
    readonly property var output: controller ? controller.output : ({})
    readonly property var devices: controller ? controller.devices : []
    readonly property string selectedId: String(output.selected_id || "")
    readonly property string selectedPortId: String(output.selected_port_id || "")
    function portName(port) { return port.kind ? t("audio_output_kind_" + port.kind) : port.name }
    function isSelected(option) {
        return option.id === selectedId && (selectedPortId ? option.portId === selectedPortId
            : !option.portId || option.portId === option.activePortId)
    }
    readonly property var options: {
        const rows = [{id: "", portId: "", detail: "", name: t("audio_output_system_default"), is_default: false, available: true}]
        for (const device of devices) {
            if (device.ports && device.ports.length) {
                for (const port of device.ports)
                    rows.push({id: device.id, portId: port.id, activePortId: device.active_port_id, name: portName(port), detail: device.name,
                               is_default: device.is_default && port.id === device.active_port_id, available: port.available})
            } else
                rows.push({id: device.id, portId: "", detail: "", name: device.name, is_default: device.is_default, available: true})
        }
        if (selectedId && !rows.some(option => isSelected(option)))
            rows.push({id: selectedId, portId: selectedPortId, detail: "", name: output.selected_port_name || output.selected_name || t("audio_output_saved_device"), is_default: false, available: false})
        return rows
    }
    readonly property string activeName: {
        const device = devices.find(item => item.id === output.active_id)
        if (!device) return ""
        const port = (device.ports || []).find(item => item.id === output.active_port_id)
        return port ? portName(port) + " · " + device.name : String(device.name)
    }
    property alias currentIndex: deviceList.currentIndex
    spacing: 8
    function t(key) { return translator.text(key, translator.language) }
    function resetSelection() {
        deviceList.currentIndex = Math.max(0, options.findIndex(item => isSelected(item)))
        deviceList.positionViewAtIndex(deviceList.currentIndex, ListView.Contain)
    }
    function focusList() { resetSelection(); deviceList.forceActiveFocus() }
    function choose(index) {
        if (!controller || !connected || controller.busy || index < 0 || index >= options.length || !options[index].available)
            return
        controller.select(String(options[index].id), String(options[index].portId))
    }
    onOptionsChanged: resetSelection()

    ListView {
        id: deviceList
        objectName: "audioOutputList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(260, root.options.length * 52)
        clip: true
        model: root.options
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        keyNavigationWraps: true
        activeFocusOnTab: true
        Accessible.name: root.t("audio_output")
        Keys.onReturnPressed: root.choose(currentIndex)
        Keys.onEnterPressed: root.choose(currentIndex)
        Keys.onSpacePressed: root.choose(currentIndex)
        ScrollBar.vertical: ScrollBar {}
        delegate: TextButton {
            id: row
            required property var modelData
            required property int index
            readonly property bool selected: root.isSelected(modelData)
            objectName: "audioOutputOption_" + index
            width: deviceList.width; height: 52
            focusPolicy: Qt.NoFocus
            text: modelData.name
            enabled: root.connected && root.controller && !root.controller.busy && modelData.available
            subtle: true
            background: Rectangle {
                radius: Theme.radiusSm
                color: row.down || row.selected ? Theme.bgSelected : row.hovered ? Theme.bgHover
                    : row.ListView.isCurrentItem && deviceList.activeFocus ? Theme.bgHover : "transparent"
                border.width: row.activeFocus ? 1 : 0
                border.color: Theme.accentPrimary
            }
            Accessible.role: Accessible.RadioButton
            Accessible.name: text
            Accessible.checkable: true
            Accessible.checked: selected
            contentItem: RowLayout {
                spacing: 8
                Label { text: row.selected ? "✓" : ""; Layout.preferredWidth: 16; color: Theme.accentPrimary }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: row.text; textFormat: Text.PlainText; elide: Text.ElideRight
                        color: !row.enabled ? Theme.textDisabled : row.selected ? Theme.accentPrimary : Theme.textSecondary
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: Boolean(row.modelData.detail)
                        text: row.modelData.detail; textFormat: Text.PlainText; elide: Text.ElideRight
                        color: Theme.textMuted; font.pixelSize: Theme.fontCaption
                    }
                }
                Label {
                    visible: row.modelData.is_default || !row.modelData.available
                    text: root.t(row.modelData.available ? "audio_output_default_badge" : "audio_output_disconnected_badge")
                    color: Theme.textMuted; font.pixelSize: Theme.fontCaption
                }
            }
            ToolTip.visible: hovered
            ToolTip.text: text + (modelData.detail ? "\n" + modelData.detail : "")
            onClicked: { deviceList.currentIndex = index; root.choose(index) }
        }
    }
    Label {
        objectName: "audioOutputStatus"
        Layout.fillWidth: true
        text: !root.connected ? root.t("audio_output_backend_disconnected")
            : root.controller && root.controller.busy ? root.t("audio_output_switching")
            : !root.output.available ? root.t(root.selectedId ? "audio_output_device_disconnected" : "audio_output_no_devices")
            : root.t("audio_output_current").arg(root.activeName)
        color: Theme.textMuted; font.pixelSize: Theme.fontCaption
        wrapMode: Text.Wrap; textFormat: Text.PlainText
    }
    Label {
        objectName: "audioOutputError"
        Layout.fillWidth: true
        visible: Boolean(root.controller && root.controller.error)
        text: root.controller ? root.t(root.controller.error) : ""
        color: Theme.statusError; wrapMode: Text.Wrap; textFormat: Text.PlainText
    }
    Label {
        objectName: "audioOutputSharedPortHint"
        Layout.fillWidth: true
        visible: root.devices.some(device => device.ports && device.ports.length > 0)
        text: root.t("audio_output_shared_port_hint")
        color: Theme.textMuted; font.pixelSize: Theme.fontCaption
        wrapMode: Text.Wrap; textFormat: Text.PlainText
    }
    Label {
        Layout.fillWidth: true
        text: root.t("audio_output_disconnect_hint")
        color: Theme.textMuted; font.pixelSize: Theme.fontCaption
        wrapMode: Text.Wrap; textFormat: Text.PlainText
    }
}
