import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitHeight: Theme.controlMinHeight
    font.pixelSize: Theme.fontBody
    leftPadding: 12
    rightPadding: 32
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
        text: "⌄"; color: Theme.textMuted; font.pixelSize: 18
    }
    background: Rectangle {
        color: Theme.bgRaised; radius: Theme.radiusSm
        border.color: control.activeFocus ? Theme.accentPrimary : Theme.borderControl
    }
    delegate: ItemDelegate {
        id: option
        required property var modelData
        required property int index
        width: control.width - 8
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: typeof option.modelData === "object" ? option.modelData[control.textRole] : option.modelData
            color: Theme.textPrimary; font: control.font; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: option.highlighted ? Theme.bgSelected : Theme.bgRaised; radius: Theme.radiusSm }
    }
    popup: Popup {
        y: control.height + 4; width: control.width; padding: 4
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)
        contentItem: ListView {
            clip: true; implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle { color: Theme.bgRaised; radius: Theme.radiusSm; border.color: Theme.borderSubtle }
    }
}
