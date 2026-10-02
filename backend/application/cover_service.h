#pragma once
#include "domain/lyrics_storage.h"
#include <QHash>
#include <QObject>
#include <memory>

namespace nekotune {
class CoverService final : public QObject {
    Q_OBJECT
  public:
    explicit CoverService(std::unique_ptr<ILyricsStorage> storage);
    QString resolve(const QString &path, const QString &trackId) const;
    void updateLyrics(const LyricsSnapshot &state);
    void assetsUpdated(const QString &trackId);
  signals:
    void changed();

  private:
    QString cachedCover(const QString &trackId) const;
    std::unique_ptr<ILyricsStorage> m_storage;
    mutable QHash<QString, QString> m_covers;
    QHash<QString, quint64> m_localVersions;
    bool m_offline = false;
};
} // namespace nekotune
