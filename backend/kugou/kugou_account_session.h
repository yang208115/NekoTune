#pragma once
#include <QHash>
#include <QString>
namespace nekotune {
class KugouAccountSession final {
  public:
    KugouAccountSession(QString keyPath, QString sessionPath);
    QString saveAccountKey(const QString &key);
    QString clearAccountKey();
    bool saveSession();
    void loadSession();
    void loadKey();
    QString key;
    bool keySaved = false;
    QHash<QString, QString> cookies;

  private:
    QString m_keyPath, m_sessionPath;
};
} // namespace nekotune
