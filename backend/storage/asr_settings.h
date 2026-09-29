#pragma once
#include <QJsonObject>
#include <QString>

namespace nekotune {
class AsrSettings final {
  public:
    explicit AsrSettings(const QString &path);
    QJsonObject publicSettings() const;
    QString apiKey() const
    {
        return m_key;
    }
    bool update(const QJsonObject &params, QString *error);

  private:
    QString m_path;
    QString m_key;
};
} // namespace nekotune
