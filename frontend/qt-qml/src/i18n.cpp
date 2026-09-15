#include "i18n.h"

#include <QFile>
#include <QLocale>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

I18n::I18n(QObject *parent)
    : QObject(parent)
{
    m_translations.insert(QStringLiteral("en"), loadCatalog(QStringLiteral(":/i18n/en.json")));
    m_translations.insert(QStringLiteral("zh"), loadCatalog(QStringLiteral(":/i18n/zh.json")));

    const auto environmentLanguage = QProcessEnvironment::systemEnvironment()
                                         .value(QStringLiteral("NEKOTUNE_LANGUAGE"));
    if (!environmentLanguage.isEmpty()) {
        m_language = normalizeLanguage(environmentLanguage);
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

void I18n::setLanguage(const QString &language)
{
    const auto normalized = normalizeLanguage(language);
    if (m_language == normalized) {
        return;
    }

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
