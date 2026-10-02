#pragma once
#include "domain/lyrics/lyrics_types.h"
#include <QNetworkAccessManager>
#include <QPointer>

namespace nekotune {
class SidecarStore final : public QObject {
    Q_OBJECT
  public:
    explicit SidecarStore(QNetworkAccessManager *manager = nullptr);
    void setCurrent(const QString &hash, quint64 revision, bool offline);
    void save(const QString &hash, quint64 revision, const QString &base, const LyricsDocument &document);
    void shutdown();
  signals:
    void saved(const QString &hash);
    void failed(const QString &hash, const QString &message);

  private:
    void fetchCover(const QString &hash, quint64 revision, quint64 generation, const QString &base,
                    const QUrl &url, int redirects = 0);
    bool current(const QString &hash, quint64 revision, quint64 generation) const;
    QNetworkAccessManager *m_manager;
    QPointer<QNetworkReply> m_reply;
    QString m_hash;
    quint64 m_revision = 0, m_generation = 0;
    bool m_offline = false;
};
} // namespace nekotune
