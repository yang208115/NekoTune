pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

Item {
    id: root
    objectName: "themeBackground"
    readonly property bool loading: backdrop.status === Image.Loading
    required property var controllers
    required property var translator
    required property var hostWindow
    readonly property var themeController: controllers.theme || null
    readonly property var config: themeController ? themeController.background : ({})
    clip: true
    Item {
        anchors.fill: parent
        visible: !custom.ready
        opacity: root.config.opacity === undefined ? 1 : root.config.opacity
        SizedImage {
            id: backdrop; anchors.fill: parent
            objectName: "themeStaticBackgroundImage"
            source: root.visible && !custom.ready && root.width > 0 && root.height > 0 ? root.config.source || "" : ""
            asynchronous: true
            cache: false
            fillMode: Image.PreserveAspectCrop
            visible: !effect.active
        }
        Loader {
            id: effect; anchors.fill: parent
            objectName: "themeStaticBackgroundEffect"
            active: GraphicsInfo.api !== GraphicsInfo.Software && backdrop.status === Image.Ready && (root.config.blur || 0) > 0
            sourceComponent: MultiEffect { source: backdrop; blurEnabled: true; blur: root.config.blur || 0; blurMax: 32; autoPaddingEnabled: false }
        }
        Rectangle {
            anchors.fill: parent
            visible: backdrop.status === Image.Ready
            color: root.config.overlayColor || Theme.bgCanvas
            opacity: root.config.overlayOpacity === undefined ? 0.65 : root.config.overlayOpacity
        }
    }
    ThemeSlot {
        id: custom; anchors.fill: parent
        active: root.visible
        themeDescriptor: root.themeController ? root.themeController.uiSlots.background || ({}) : ({})
        controllers: root.controllers; translator: root.translator; hostWindow: root.hostWindow
    }
}
