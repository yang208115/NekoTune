#include "i18n.h"
#include "app_paths.h"
#include <QDebug>

#include <QFile>
#include <QLocale>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

// Language precedence is explicit environment, saved preference, then system.
// Both supported catalogs are embedded resources loaded once at startup.
// Unsupported language variants normalize to English except zh variants.
// Passing language into text() bindings makes catalog changes observable
// to QML without recreating every page or controller.
I18n::I18n(QObject *parent)
    : QObject(parent)
{
    m_translations.insert(QStringLiteral("en"), loadCatalog(QStringLiteral(":/i18n/en.json")));
    m_translations.insert(QStringLiteral("zh"), loadCatalog(QStringLiteral(":/i18n/zh.json")));

    const auto environmentLanguage = QProcessEnvironment::systemEnvironment()
                                         .value(QStringLiteral("NEKOTUNE_LANGUAGE"));
    if (!environmentLanguage.isEmpty()) {
        m_language = normalizeLanguage(environmentLanguage);
    } else if (const auto saved = nekotune::AppPaths::setting("language"); saved.isString()) {
        m_language = normalizeLanguage(saved.toString());
    } else {
        m_language = QLocale::system().language() == QLocale::Chinese
            ? QStringLiteral("zh")
            : QStringLiteral("en");
    }
}

QString I18n::language() const
{
    return m_language;
}

// Apply the user's choice immediately even if persistence fails.
// The warning describes failure to restore that choice on next startup.
// Other settings keys remain protected by AppPaths' merged atomic write.
// Only an actual normalized change emits the language notification.
void I18n::setLanguage(const QString &language)
{
    const auto normalized = normalizeLanguage(language);
    if (m_language == normalized) {
        return;
    }

    if (!nekotune::AppPaths::saveSetting("language", normalized))
        qWarning() << "Cannot save NekoTune language preference";
    m_language = normalized;
    emit languageChanged();
}

QStringList I18n::languages() const
{
    return {QStringLiteral("en"), QStringLiteral("zh")};
}

QString I18n::text(const QString &key, const QString &locale) const
{
    const auto selectedLanguage = locale.isEmpty() ? m_language : normalizeLanguage(locale);
    const auto selected = m_translations.value(selectedLanguage);
    return selected.value(key);
}

QString I18n::countText(const QString &key, int count, const QString &locale) const
{
    const auto selectedLanguage = locale.isEmpty() ? m_language : normalizeLanguage(locale);
    const auto unitKey = count == 1 ? QStringLiteral("track") : key;
    const auto unit = text(unitKey, selectedLanguage);
    return QStringLiteral("%1 %2").arg(count).arg(unit);
}

QHash<QString, QString> I18n::loadCatalog(const QString &resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto object = document.object();
    QHash<QString, QString> catalog;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        catalog.insert(it.key(), it.value().toString());
    }
    return catalog;
}

QString I18n::normalizeLanguage(const QString &language)
{
    return language.toLower().startsWith(QStringLiteral("zh"))
        ? QStringLiteral("zh")
        : QStringLiteral("en");
}
