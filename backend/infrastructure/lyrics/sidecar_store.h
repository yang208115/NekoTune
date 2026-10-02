#pragma once
#include "domain/lyrics/lyrics_types.h"
#include <QNetworkAccessManager>
#include <QPointer>

namespace nekotune {
/// Saves selected lyrics/artwork beside managed audio; late cover replies cannot replace newer assets.
class SidecarStore final : public QObject {
    Q_OBJECT
  public:
    explicit SidecarStore(QNetworkAccessManager *manager = nullptr);
    /// Define the only hash/revision currently allowed to install managed sidecars.
    /// Changing identity or offline state invalidates any in-flight artwork generation.
    /// Call this before save so a late reply cannot publish assets for an old selection.
    void setCurrent(const QString &hash, quint64 revision, bool offline);
    /// @param base App-owned asset basename, without a lyric or image extension.
    /// @param document Selected lyric content and optional provider artwork hint.
    /// Hash/revision must match setCurrent; stale calls and empty basenames are ignored.
    /// Lyric and optional cover outcomes are signaled independently and may partially succeed.
    /// Existing symlinks are rejected rather than followed when replacing assets.
    void save(const QString &hash, quint64 revision, const QString &base, const LyricsDocument &document);
    /// Invalidate the current asset generation before aborting its network reply.
    /// Aborting a reply can emit completion, so callbacks are disconnected first.
    /// Already published local lyric files are not removed by cancellation.
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
