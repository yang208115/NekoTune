#pragma once
#include "infrastructure/credentials/credential_store.h"
#include <QHash>
#include <QString>
#include <QStringList>
namespace nekotune {
/// Owns backend-private account key and cookie state.
/// Path-like IDs namespace secure-store entries by application profile.
/// Legacy plaintext is a migration input, never a new write destination.
/// Migration validates and verifies the secure copy before removing input.
/// Launch environment keys remain caller-owned overrides after clearing.
/// Status consumers expose saved/configured flags rather than secret text.
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
