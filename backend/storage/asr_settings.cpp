#include "storage/asr_settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

namespace nekotune {
AsrSettings::AsrSettings(const QString &path) : m_path(path)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly) && file.size() <= 16384) {
        const auto json = QJsonDocument::fromJson(file.readAll()).object();
        m_key = json.value(QStringLiteral("api_key")).toString();
    }
}
QJsonObject AsrSettings::publicSettings() const
{
    return {{QStringLiteral("api_key_configured"), !m_key.isEmpty()},
            {QStringLiteral("model"), QStringLiteral("fun-asr")}};
}
bool AsrSettings::update(const QJsonObject &params, QString *error)
{
    QString key = m_key;
    if (params.contains(QStringLiteral("api_key"))) {
        const auto value = params.value(QStringLiteral("api_key"));
        if (!value.isString() || value.toString().size() > 4096 || value.toString().contains('\n') ||
            value.toString().contains('\r')) {
            *error = QStringLiteral("Invalid API key");
            return false;
        }
        key = value.toString().trimmed();
        if (std::any_of(key.cbegin(), key.cend(),
                        [](QChar character) { return character.unicode() < 33 || character.unicode() > 126; })) {
            *error = QStringLiteral("Invalid API key");
            return false;
        }
    }
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    const auto bytes =
        QJsonDocument(QJsonObject{{QStringLiteral("api_key"), key}}).toJson();
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()) || !file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || file.write(bytes) != bytes.size() ||
        !file.commit()) {
        *error = QStringLiteral("Unable to save ASR settings");
        return false;
    }
    m_key = key;
    return true;
}
} // namespace nekotune
