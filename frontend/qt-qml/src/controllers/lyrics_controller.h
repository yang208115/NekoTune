#pragma once
#include "controllers/feature_controller.h"
#include <QVariantList>
/// Consumes structured backend lyric state without fetching providers itself.
/// Source descriptors are loaded after each backend connection.
/// Only searchable sources are exposed for the manual search picker.
/// track/revision/index are forwarded together for candidate selection.
/// Revision stays a string so QML does not round a 64-bit identifier.
/// The current document is separate from editable song lyric text.
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
    /// Submit editable search metadata without changing the library song's stored fields.
    /// The track hash constrains the request to the currently playing audio identity.
    /// source is the registered provider ID from sources, not a translated label.
    Q_INVOKABLE void searchLyrics(const QString &track, const QString &title, const QString &artist,
                                  const QString &album, const QString &source) {
        send("lyrics.search", {{"track_id", track},
                               {"title", title},
                               {"artist", artist},
                               {"album", album},
                               {"source", source}});
    }
    /// Forward the track, exact revision string and index from one candidate snapshot together.
    /// An index alone is unsafe after a new search replaces the candidate list.
    /// The backend checks both identity and revision before resolving or applying the selection.
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
