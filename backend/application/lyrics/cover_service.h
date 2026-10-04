#pragma once
#include "domain/lyrics/lyrics_storage.h"
#include <QHash>
#include <QObject>
#include <memory>
#include <functional>

namespace nekotune {
/// Resolves the common artwork URL for every view; local sidecars precede cached remote artwork.
/// Returns an empty URL when no local artwork exists and remote artwork is disabled or absent.
class CoverService final : public QObject {
    Q_OBJECT
  public:
    explicit CoverService(std::unique_ptr<ILyricsStorage> storage);
    QString resolve(const QString &path, const QString &trackId) const;
    void updateLyrics(const LyricsSnapshot &state);
    void assetsUpdated(const QString &trackId);
    void setManagedBaseResolver(std::function<QString(const QString &)> resolver) {
        m_managedBase = std::move(resolver);
    }
  signals:
    void changed();

  private:
    QString cachedCover(const QString &trackId) const;
    std::unique_ptr<ILyricsStorage> m_storage;
    mutable QHash<QString, QString> m_covers;
    QHash<QString, quint64> m_localVersions;
    bool m_offline = false;
    std::function<QString(const QString &)> m_managedBase;
};
} // namespace nekotune
