import QtQuick
import NekoTune 1.0
import "../components"
Item {
    id: root
    required property var modelData
    required property var shell
    required property var controllers
    required property var translator
    required property var transport
    property bool active: true
    property bool activated: false
    readonly property bool loadActive: active && activated
    property var extensions: controllers.extensions || null
    readonly property var replacement: modelData.extensionId ? modelData : extensions ? extensions.activeSlots["page:" + modelData.id] || ({}) : ({})
    readonly property var item: extension.ready ? extension.item : nativeLoader.item
    readonly property int status: extension.ready ? Loader.Ready : nativeLoader.status
    function activate() {
        if (!active) return
        activated = true
        loadNative()
    }
    function loadNative() {
        if (loadActive && !modelData.extensionId && nativeLoader && nativeLoader.source.toString() === "")
            nativeLoader.setSource(/^[a-z][a-z0-9+.-]*:/i.test(modelData.source) ? modelData.source : Qt.resolvedUrl("../" + modelData.source), {shell: root.shell, controllers: root.controllers, transport: root.transport, translator: root.translator})
    }
    Component.onCompleted: if (visible) activate()
    onVisibleChanged: if (visible) activate()
    onActiveChanged: if (visible) activate()
    Loader { id: nativeLoader; anchors.fill: parent; active: root.loadActive && !root.modelData.extensionId; visible: !extension.ready }
    ThemeSlot {
        id: extension; objectName: "themePageSlot_" + root.modelData.id; anchors.fill: parent
        active: root.loadActive
        themeActive: root.loadActive
        themeDescriptor: !root.modelData.extensionId && root.controllers.theme ? root.controllers.theme.uiSlots["page:" + root.modelData.id] || ({}) : ({})
        fallbackDescriptor: root.replacement
        controllers: root.controllers; translator: root.translator; hostWindow: root.shell
    }
    Text { anchors.centerIn: parent; width: parent.width - 40; wrapMode: Text.Wrap; color: Theme.statusError; text: extension.error; visible: root.modelData.extensionId !== undefined && !extension.ready }
}
