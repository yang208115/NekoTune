#pragma once
#include <QJsonValue>
#include <QString>

namespace nekotune::AppPaths {
QString musicDirectory();
QString configDirectory();
QString configFile(const QString &name);
QString databasePath();
bool prepare(QString *error = nullptr);
QJsonValue setting(const QString &key);
bool saveSetting(const QString &key, const QJsonValue &value);
} // namespace nekotune::AppPaths
