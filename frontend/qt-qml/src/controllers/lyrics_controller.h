#pragma once
#include "controllers/feature_controller.h"
#include <QVariantList>
class LyricsController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
  public:
    explicit LyricsController(IpcClient &client);
    QVariantMap current() const { return m_current; }
    QVariantList sources() const { return m_sources; }
    Q_INVOKABLE void refreshLyrics(const QString &track) { send("lyrics.refresh", {{"track_id", track}}); }
    Q_INVOKABLE void setLyricsOffline(bool offline) { send("lyrics.set_offline", {{"offline", offline}}); }
    Q_INVOKABLE void searchLyrics(const QString &track, const QString &title, const QString &artist,
                                  const QString &album, const QString &source) {
        send("lyrics.search", {{"track_id", track},
                               {"title", title},
                               {"artist", artist},
                               {"album", album},
                               {"source", source}});
    }
    Q_INVOKABLE void selectLyrics(const QString &track, const QString &revision, int index) {
        send("lyrics.select", {{"track_id", track}, {"revision", revision}, {"index", index}});
    }
  signals:
    void changed();
    void sourcesChanged();

  private:
    void apply(const QJsonObject &data);
    QVariantMap m_current;
    QVariantList m_sources;
};
