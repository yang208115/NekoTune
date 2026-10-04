import QtQuick
import "translations.js" as Translations
QtObject {
    property string language: i18n.language
    onLanguageChanged: if (i18n.language !== language) i18n.language = language
    function text(key, locale) {
        const messages = Translations.messages[locale] || Translations.messages.en
        return messages[key] || i18n.text(key, locale)
    }
}
