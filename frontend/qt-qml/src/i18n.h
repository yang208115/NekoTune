#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

class I18n final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QStringList languages READ languages CONSTANT)

public:
    explicit I18n(QObject *parent = nullptr);

    QString language() const;
    void setLanguage(const QString &language);
    QStringList languages() const;

    Q_INVOKABLE QString text(const QString &key, const QString &locale = {}) const;
    Q_INVOKABLE QString countText(const QString &key, int count, const QString &locale = {}) const;

signals:
    void languageChanged();

private:
    static QString normalizeLanguage(const QString &language);
    static QHash<QString, QString> loadCatalog(const QString &resourcePath);

    QString m_language;
    QHash<QString, QHash<QString, QString>> m_translations;
};
