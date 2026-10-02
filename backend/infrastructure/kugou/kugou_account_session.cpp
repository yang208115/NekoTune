#include "infrastructure/kugou/kugou_account_session.h"
#include "app_paths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace nekotune {
namespace {
bool validKey(const QByteArray &bytes) {
    const auto value = QString::fromUtf8(bytes).trimmed();
    return !value.isEmpty() && bytes.size() <= 4096 && value.toUtf8() == bytes.trimmed() &&
           !value.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
}
bool validSession(const QByteArray &bytes) {
    const auto document = QJsonDocument::fromJson(bytes);
    const auto object = document.object();
    if (!document.isObject() || object.value("version").toInt() != 1 ||
        !object.value("cookies").isObject())
        return false;
    const auto cookies = object.value("cookies").toObject();
    for (auto it = cookies.begin(); it != cookies.end(); ++it)
        if (!it.value().isString())
            return false;
    return true;
}
} // namespace
KugouAccountSession::KugouAccountSession(QString keyPath, QString sessionPath,
                                       std::shared_ptr<CredentialStore> store)
    : m_keyPath(std::move(keyPath)), m_sessionPath(std::move(sessionPath)), m_store(std::move(store)) {
    loadKey();
    loadSession();
}
QStringList KugouAccountSession::legacyPaths(const QString &path) const {
    QStringList paths{path};
    // Isolated profiles and explicitly supplied paths must never import real credentials.
    if (qEnvironmentVariableIsSet("NEKOTUNE_HOME"))
        return paths;
    const bool isKey = path == m_keyPath;
    const auto name = isKey ? "kugou-account-key" : "kugou-session.json";
    if (QFileInfo(path).absoluteFilePath() == AppPaths::configFile(name)) {
        const auto location = isKey ? QStandardPaths::GenericConfigLocation
                                    : QStandardPaths::GenericDataLocation;
        paths.append(QDir(QStandardPaths::writableLocation(location)).filePath("NekoTune/" + QString(name)));
    }
    paths.removeDuplicates();
    return paths;
}
Result<void> KugouAccountSession::removeLegacy(const QString &path) {
    for (const auto &candidate : legacyPaths(path)) {
        const QFileInfo info(candidate);
        if (info.isSymLink() || (info.exists() && (!info.isFile() || !QFile::remove(candidate))))
            return failure(QStringLiteral("Credentials secured, but cannot remove legacy plaintext file"),
                           ErrorCode::Storage);
    }
    return {};
}
Result<void> KugouAccountSession::saveCredential(const QString &path, const QByteArray &bytes) {
    auto saved = m_store->write(path, bytes);
    if (!saved)
        return saved;
    const auto verified = m_store->read(path);
    if (!verified || !verified.value() || *verified.value() != bytes)
        return failure(QStringLiteral("Cannot verify system keyring write; legacy files retained"),
                       ErrorCode::Storage);
    return removeLegacy(path);
}
Result<std::optional<QByteArray>> KugouAccountSession::loadCredential(const QString &path, qsizetype limit) {
    auto stored = m_store->read(path);
    if (!stored)
        return stored;
    auto valid = [&](const QByteArray &bytes) {
        return bytes.size() <= limit && (path == m_keyPath ? validKey(bytes) : validSession(bytes));
    };
    if (stored.value()) {
        if (!valid(*stored.value()))
            return failure(QStringLiteral("Invalid credentials in system keyring"), ErrorCode::Storage);
        auto cleanup = removeLegacy(path);
        if (!cleanup)
            (path == m_keyPath ? keyError : sessionError) = cleanup.error().message;
        return stored;
    }
    for (const auto &candidate : legacyPaths(path)) {
        const QFileInfo info(candidate);
        if (!info.exists() && !info.isSymLink())
            continue;
        QFile file(candidate);
        if (info.isSymLink() || !info.isFile() ||
            !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
            !file.open(QIODevice::ReadOnly) || file.size() > limit)
            return failure(QStringLiteral("Cannot read legacy credentials safely"), ErrorCode::Storage);
        const auto bytes = file.readAll();
        file.close();
        if (!valid(bytes))
            return failure(QStringLiteral("Invalid legacy credentials; file retained"), ErrorCode::Storage);
        const auto saved = saveCredential(path, bytes);
        if (!saved)
            return saved.error();
        return std::optional<QByteArray>{bytes};
    }
    return std::optional<QByteArray>{};
}
void KugouAccountSession::loadKey() {
    key.clear();
    keySaved = false;
    keyError.clear();
    const auto stored = loadCredential(m_keyPath, 4096);
    if (!stored)
        keyError = stored.error().message;
    else if (stored.value()) {
        key = QString::fromUtf8(*stored.value()).trimmed();
        keySaved = true;
        return;
    }
    const auto env = QProcessEnvironment::systemEnvironment();
    key = env.value(QStringLiteral("KUGOU_ACCOUNT_API_KEY")).trimmed();
    if (!key.isEmpty())
        return;
    // Explicit external files are caller-managed; never migrate or delete them.
    QFile file(env.value(QStringLiteral("KUGOU_ACCOUNT_API_KEY_FILE")));
    if (!file.fileName().isEmpty() && file.open(QIODevice::ReadOnly) && file.size() <= 4096) {
        const auto bytes = file.readAll();
        if (validKey(bytes))
            key = QString::fromUtf8(bytes).trimmed();
    }
}
QString KugouAccountSession::saveAccountKey(const QString &value) {
    const auto bytes = value.trimmed().toUtf8();
    if (!validKey(bytes))
        return QStringLiteral("Kugou account key must be 1 to 4096 printable bytes");
    const auto saved = saveCredential(m_keyPath, bytes);
    if (!saved) {
        keyError = saved.error().message;
        return keyError;
    }
    key = value.trimmed();
    keySaved = true;
    keyError.clear();
    return {};
}
QString KugouAccountSession::clearAccountKey() {
    // Remove legacy copies first so a restart cannot restore a cleared key.
    const auto cleanup = removeLegacy(m_keyPath);
    if (!cleanup)
        return cleanup.error().message;
    const auto removed = m_store->remove(m_keyPath);
    if (!removed)
        return removed.error().message;
    loadKey();
    return keyError;
}
void KugouAccountSession::loadSession() {
    cookies.clear();
    sessionError.clear();
    const auto stored = loadCredential(m_sessionPath, 16 * 1024);
    if (!stored) {
        sessionError = stored.error().message;
        return;
    }
    if (!stored.value())
        return;
    const auto values = QJsonDocument::fromJson(*stored.value()).object().value("cookies").toObject();
    for (auto it = values.begin(); it != values.end(); ++it)
        cookies.insert(it.key(), it.value().toString());
}
bool KugouAccountSession::saveSession() {
    QJsonObject values;
    for (auto it = cookies.cbegin(); it != cookies.cend(); ++it)
        values.insert(it.key(), it.value());
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"cookies", values}})
                           .toJson(QJsonDocument::Compact);
    if (bytes.size() > 16 * 1024) {
        sessionError = QStringLiteral("Kugou session exceeds storage limit");
        return false;
    }
    const auto saved = saveCredential(m_sessionPath, bytes);
    sessionError = saved ? QString() : saved.error().message;
    return bool(saved);
}
} // namespace nekotune
