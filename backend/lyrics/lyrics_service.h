#pragma once

#include "lyrics/lyrics_cache.h"
#include "lyrics/lyrics_provider.h"
#include <QObject>

namespace nekotune {

class LyricsService final : public QObject {
    Q_OBJECT
  public:
    explicit LyricsService(LyricsProvider *provider = nullptr, const QString &cacheDirectory = {},
                           QObject *parent = nullptr);
    void load(const LyricsQuery &query, const QString &path, const QString &customLyrics, quint64 revision,
              bool metadataReady, bool force = false);
    void search(const LyricsQuery &query, quint64 revision);
    void select(int index, quint64 revision);
    void clear(quint64 revision);
    void setOffline(bool offline);

    static QString normalizeForMatch(const QString &value);
    static double matchScore(const LyricsQuery &query, const LyricsCandidate &candidate);
    static QVector<LyricsCandidate> rankCandidates(const LyricsQuery &query, QVector<LyricsCandidate> candidates);
    static bool confident(const LyricsQuery &query, const QVector<LyricsCandidate> &ranked);

  signals:
    void changed(const QJsonObject &snapshot);

  private:
    void completed(quint64 token, const QVector<LyricsCandidate> &candidates);
    void failed(quint64 token, const QString &kind);
    void publish(const QString &state, const QString &error = {});
    void apply(const LyricsDocument &document, bool cache);
    void cancel();

    LyricsCache m_cache;
    LyricsProvider *m_provider;
    LyricsQuery m_query;
    quint64 m_token = 0;
    quint64 m_revision = 0;
    bool m_search = false;
    bool m_manual = false;
    bool m_offline = false;
    QVector<LyricsCandidate> m_candidates;
    QJsonObject m_snapshot;
};

} // namespace nekotune
