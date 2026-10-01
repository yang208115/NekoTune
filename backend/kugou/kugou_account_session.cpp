#include "kugou/kugou_account_session.h"
#include <QSaveFile>

#include "domain/krc_parser.h"
#include "domain/lrc_parser.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

#include <limits>

namespace nekotune {
KugouAccountSession::KugouAccountSession(QString keyPath, QString sessionPath)
    : m_keyPath(std::move(keyPath)), m_sessionPath(std::move(sessionPath)) {
    loadKey();
    loadSession();
}
void KugouAccountSession::loadKey() {
    key.clear();
    keySaved = false;
    const QFileInfo stored(m_keyPath);
    if (stored.exists() && !stored.isSymLink() &&
        QFile::setPermissions(m_keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        QFile file(m_keyPath);
        if (file.open(QIODevice::ReadOnly) && file.size() <= 4096) {
            const auto candidate = QString::fromUtf8(file.readAll()).trimmed();
            if (!candidate.isEmpty() && candidate.size() <= 4096 &&
                !candidate.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")))) {
                key = candidate;
                keySaved = true;
                return;
            }
        }
    }
    const auto env = QProcessEnvironment::systemEnvironment();
    key = env.value(QStringLiteral("KUGOU_ACCOUNT_API_KEY")).trimmed();
    if (!key.isEmpty())
        return;
    const auto filePath = env.value(QStringLiteral("KUGOU_ACCOUNT_API_KEY_FILE"));
    QFile file(filePath);
    if (!filePath.isEmpty() && file.open(QIODevice::ReadOnly) && file.size() <= 4096)
        key = QString::fromUtf8(file.readAll()).trimmed();
}

QString KugouAccountSession::saveAccountKey(const QString &key) {
    const auto trimmed = key.trimmed();
    const auto bytes = trimmed.toUtf8();
    if (trimmed.isEmpty() || bytes.size() > 4096 ||
        trimmed.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]"))))
        return QStringLiteral("Kugou account key must be 1 to 4096 printable bytes");
    if (QFileInfo(m_keyPath).isSymLink() || !QDir().mkpath(QFileInfo(m_keyPath).absolutePath()))
        return QStringLiteral("Cannot prepare private Kugou key file");
    QSaveFile file(m_keyPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot write private Kugou key file");
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file.cancelWriting();
        return QStringLiteral("Cannot secure private Kugou key file");
    }
    if (file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Cannot save private Kugou key file");
    if (!QFile::setPermissions(m_keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        QFile::remove(m_keyPath);
        return QStringLiteral("Cannot secure private Kugou key file");
    }
    this->key = trimmed;
    keySaved = true;
    return {};
}

QString KugouAccountSession::clearAccountKey() {
    const QFileInfo stored(m_keyPath);
    if (stored.isSymLink() || (stored.exists() && !QFile::remove(m_keyPath)))
        return QStringLiteral("Cannot remove private Kugou key file");
    loadKey();
    return {};
}

void KugouAccountSession::loadSession() {
    const QFileInfo info(m_sessionPath);
    if (info.isSymLink() ||
        (info.exists() &&
         !QFile::setPermissions(m_sessionPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)))
        return;
    QFile file(m_sessionPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024)
        return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value(QStringLiteral("version")).toInt() != 1)
        return;
    const auto cookies = object.value(QStringLiteral("cookies")).toObject();
    for (auto it = cookies.begin(); it != cookies.end(); ++it)
        if (it.value().isString())
            this->cookies.insert(it.key(), it.value().toString());
}

bool KugouAccountSession::saveSession() {
    if (QFileInfo(m_sessionPath).isSymLink())
        return false;
    if (!QDir().mkpath(QFileInfo(m_sessionPath).absolutePath()))
        return false;
    QSaveFile file(m_sessionPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QJsonObject cookies;
    for (auto it = this->cookies.cbegin(); it != this->cookies.cend(); ++it)
        cookies.insert(it.key(), it.value());
    const auto bytes =
        QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("cookies"), cookies}})
            .toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit())
        return false;
    return QFile::setPermissions(m_sessionPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
} // namespace nekotune
