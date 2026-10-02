#pragma once

#include "domain/lyrics_provider.h"
#include "domain/lyrics_storage.h"
#include <QHash>
#include <QObject>
#include <memory>

namespace nekotune {

class LyricsService final : public QObject {
    Q_OBJECT
  public:
    explicit LyricsService(const QVector<LyricsProvider *> &providers,
                           std::unique_ptr<ILyricsStorage> storage, QObject *parent = nullptr);
    QVector<LyricsSource> sources() const;
    bool offline() const { return m_offline; }
    void shutdown() { cancel(); }
    void load(const LyricsQuery &query, const QString &path, const QString &customLyrics, quint64 revision,
              bool metadataReady, bool force = false);
    void search(const LyricsQuery &query, quint64 revision, const QString &source = QStringLiteral("lrclib"));
    void select(int index, quint64 revision);
    void clear(quint64 revision);
    void setOffline(bool offline);

    static QString normalizeForMatch(const QString &value);
    static double matchScore(const LyricsQuery &query, const LyricsCandidate &candidate);
    static QVector<LyricsCandidate> rankCandidates(const LyricsQuery &query,
                                                   QVector<LyricsCandidate> candidates);
    static bool confident(const LyricsQuery &query, const QVector<LyricsCandidate> &ranked);

  signals:
    void changed(const nekotune::LyricsSnapshot &snapshot);
    void assetsReady(const nekotune::LyricsQuery &query, const nekotune::LyricsDocument &document,
                     quint64 revision);

  private:
    void completed(quint64 token, const QVector<LyricsCandidate> &candidates);
    void failed(quint64 token, const QString &kind);
    void publish(const QString &state, const QString &error = {});
    void apply(const LyricsDocument &document, bool cache);
    void cancel();

    std::unique_ptr<ILyricsStorage> m_storage;
    QHash<QString, LyricsProvider *> m_providers;
    LyricsProvider *m_defaultProvider = nullptr;
    LyricsProvider *m_provider;
    LyricsQuery m_query;
    quint64 m_token = 0;
    quint64 m_revision = 0;
    bool m_search = false;
    bool m_manual = false;
    bool m_offline = false;
    QVector<LyricsCandidate> m_candidates;
    LyricsSnapshot m_snapshot;
};

} // namespace nekotune
