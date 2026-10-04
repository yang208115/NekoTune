#pragma once
#include <QJsonValue>
#include <QString>

// Central path policy shared by the embedded backend and standalone client.
// NEKOTUNE_HOME selects an isolated root for development and tests.
// All app-owned settings/cache paths resolve below its config directory.
// prepare() performs additive migration only for the default profile.
// saveSetting() merges one key atomically while retaining other keys.
// Callers still treat preparation/persistence errors as operation failures.
namespace nekotune::AppPaths {
QString musicDirectory();
QString configDirectory();
QString configFile(const QString &name);
QString databasePath();
QString extensionsDirectory();
bool prepare(QString *error = nullptr);
QJsonValue setting(const QString &key);
bool saveSetting(const QString &key, const QJsonValue &value);
} // namespace nekotune::AppPaths
