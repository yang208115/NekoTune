#include "app_paths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace nekotune::AppPaths {
QString musicDirectory() {
    const auto override = qEnvironmentVariable("NEKOTUNE_HOME");
    return QDir::cleanPath(override.isEmpty() ? QDir::home().filePath("Music/NekoTune")
                                              : QFileInfo(override).absoluteFilePath());
}
QString configDirectory() { return QDir(musicDirectory()).filePath("config"); }
QString configFile(const QString &name) { return QDir(configDirectory()).filePath(name); }
QString databasePath() { return configFile("nekotune.sqlite3"); }
QString extensionsDirectory() { return configFile("extensions"); }
namespace {
bool copyMissing(const QString &source, const QString &target) {
    const QFileInfo info(source);
    if (!info.exists() || info.isSymLink())
        return true;
    if (QFileInfo(target).isSymLink())
        return false;
    if (info.isDir()) {
        if (!QDir().mkpath(target))
            return false;
        for (const auto &entry : QDir(source).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
            if (!copyMissing(entry.absoluteFilePath(), QDir(target).filePath(entry.fileName())))
                return false;
        return true;
    }
    if (QFileInfo::exists(target))
        // Migration is additive: destination settings/assets win, and legacy files stay recoverable.
        return true;
    QFile input(source);
    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
        return false;
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    while (!input.atEnd()) {
        const auto bytes = input.read(1024 * 1024);
        if (input.error() != QFileDevice::NoError || output.write(bytes) != bytes.size())
            return false;
    }
    return output.commit();
}
} // namespace
bool prepare(QString *error) {
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (QFileInfo(configDirectory()).isSymLink() || !QDir().mkpath(configDirectory()) ||
        !QFile::setPermissions(configDirectory(),
                               QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return fail("Unable to prepare NekoTune config directory");
    // An explicit home is an isolated profile; never import the user's real
    // credentials into it.
    if (!qEnvironmentVariable("NEKOTUNE_HOME").isEmpty())
        return true;
    if (QFileInfo::exists(configFile(".legacy-migrated")))
        return true;
    const auto data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const auto cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    const QList<QPair<QString, QString>> sources{
        {QDir(data).filePath("NekoTune/NekoTune/lyrics-cache"), configFile("lyrics-cache")},
        {QDir(data).filePath("NekoTune/NekoTune Backend/lyrics-cache"), configFile("lyrics-cache")},
        {QDir(cache).filePath("NekoTune/NekoTune/covers"), configFile("covers")}};
    for (const auto &[source, target] : sources)
        if (!copyMissing(source, target))
            return fail("Unable to migrate NekoTune file: " + source);
    for (const auto &name : {"kugou-account-key", "kugou-session.json"})
        if (QFileInfo::exists(configFile(name)) &&
            !QFile::setPermissions(configFile(name), QFileDevice::ReadOwner | QFileDevice::WriteOwner))
            return fail("Unable to secure NekoTune account file");
    QSaveFile marker(configFile(".legacy-migrated"));
    marker.setDirectWriteFallback(false);
    if (!marker.open(QIODevice::WriteOnly) || marker.write("1\n") != 2 || !marker.commit())
        return fail("Unable to finish NekoTune configuration migration");
    return true;
}
QJsonValue setting(const QString &key) {
    QFile file(configFile("settings.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(key);
}
bool saveSetting(const QString &key, const QJsonValue &value) {
    if (!prepare())
        return false;
    // Serialize the entire read-modify-write, not just the atomic rename, to preserve unrelated keys.
    QLockFile lock(configFile("settings.lock"));
    if (!lock.tryLock(1000))
        return false;
    QJsonObject object;
    QFile input(configFile("settings.json"));
    if (input.exists()) {
        if (!input.open(QIODevice::ReadOnly) || input.size() > 64 * 1024)
            return false;
        const auto document = QJsonDocument::fromJson(input.readAll());
        if (!document.isObject())
            return false;
        object = document.object();
    }
    object.insert(key, value);
    QSaveFile output(configFile("settings.json"));
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
        return false;
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = QJsonDocument(object).toJson();
    return output.write(bytes) == bytes.size() && output.commit();
}
} // namespace nekotune::AppPaths
