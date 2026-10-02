#pragma once
#include "infrastructure/credentials/credential_store.h"
#include <QHash>
#include <QString>
#include <QStringList>
namespace nekotune {
class KugouAccountSession final {
  public:
    KugouAccountSession(QString keyPath, QString sessionPath,
                        std::shared_ptr<CredentialStore> store = systemCredentialStore());
    QString saveAccountKey(const QString &key);
    QString clearAccountKey();
    bool saveSession();
    void loadSession();
    void loadKey();
    QString key;
    bool keySaved = false;
    QHash<QString, QString> cookies;
    QString keyError, sessionError;

  private:
    QString m_keyPath, m_sessionPath;
    std::shared_ptr<CredentialStore> m_store;
    QStringList legacyPaths(const QString &path) const;
    Result<std::optional<QByteArray>> loadCredential(const QString &path, qsizetype limit);
    Result<void> saveCredential(const QString &path, const QByteArray &bytes);
    Result<void> removeLegacy(const QString &path);
};
} // namespace nekotune
