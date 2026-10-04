import QtQuick
Item {
    objectName: "kugouPluginSettings"
    Controller { id: controller }
    Translator { id: pluginTranslator }
    SettingsPanel { anchors.fill: parent; client:controller; account:controller.account; translator:pluginTranslator }
}
