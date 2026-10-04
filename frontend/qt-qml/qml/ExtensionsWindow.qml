import QtQuick
import QtQuick.Controls
import "components"
ApplicationWindow {
    id: window
    width: 960; height: 720; minimumWidth: 760; minimumHeight: 580
    title: "NekoTune · " + i18n.text("extensions", i18n.language)
    color: "#0E0D14"
    ExtensionsPanel { anchors.fill: parent; anchors.margins: 24; client: controllers.extensions; translator: i18n }
}
