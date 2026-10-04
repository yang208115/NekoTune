import QtQuick
import NekoTune 1.0
Item {
    id: root
    required property var modelData
    required property var shell
    required property var controllers
    required property var translator
    required property var transport
    property bool active: true
    property var extensions: controllers.extensions || null
    readonly property var replacement: modelData.extensionId ? modelData : extensions ? extensions.activeSlots["page:" + modelData.id] || ({}) : ({})
    readonly property var item: extension.ready ? extension.item : nativeLoader.item
    readonly property int status: extension.ready ? Loader.Ready : nativeLoader.status
    function loadNative() {
        if (active && !modelData.extensionId) nativeLoader.setSource(/^[a-z][a-z0-9+.-]*:/i.test(modelData.source) ? modelData.source : Qt.resolvedUrl("../" + modelData.source), {shell: root.shell, controllers: root.controllers, transport: root.transport, translator: root.translator})
    }
    Component.onCompleted: loadNative()
    onActiveChanged: loadNative()
    Loader { id: nativeLoader; anchors.fill: parent; active: root.active && !root.modelData.extensionId; visible: !extension.ready }
    ExtensionView {
        id: extension; anchors.fill: parent
        descriptor: root.active ? root.replacement : ({})
        controllers: root.controllers; translator: root.translator; hostWindow: root.shell
        visible: ready
    }
    Text { anchors.centerIn: parent; width: parent.width - 40; wrapMode: Text.Wrap; color: "#FF9BAE"; text: extension.error; visible: root.modelData.extensionId !== undefined && !extension.ready }
}
