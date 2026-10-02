#pragma once
#include "application/lyrics/lyrics_service.h"
#include "application/playback/player_engine.h"
#include <QThread>
namespace nekotune {
class LyricsController final : public QObject {
    Q_OBJECT
  public:
    LyricsController(PlayerEngine &player, LyricsService *service);
    ~LyricsController() override;
    const LyricsSnapshot &snapshot() const { return m_snapshot; }
    const QVector<LyricsSource> &sources() const { return m_sources; }
    Result<void> refresh(const QString &trackId);
    Result<void> search(const QString &trackId, const MetadataPatch &fields, const QString &album,
                        const QString &source);
    Result<void> select(const QString &trackId, quint64 revision, int index);
    void setOffline(bool offline);
    void shutdown();
  signals:
    void changed(const nekotune::LyricsSnapshot &snapshot);
    void assetsReady(const QString &trackId, const nekotune::LyricsDocument &document, quint64 revision);

  private:
    LyricsQuery query() const;
    void load(bool metadataReady, bool force = false);
    PlayerEngine &m_player;
    LyricsService *m_service;
    QThread m_thread;
    QVector<LyricsSource> m_sources;
    LyricsSnapshot m_snapshot;
    quint64 m_revision = 0;
    bool m_ready = false;
};
} // namespace nekotune
