#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>
#include <optional>

namespace nekotune {

struct LyricLine {
    qint64 timestampMs = -1;
    QString text;
};

struct LyricsQuery {
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0;
    QString trackId;
};

struct LyricsDocument {
    QString source;
    QString coverUrl;
    QString syncedLyrics;
    QString krcLyrics;
    QString plainLyrics;
    LyricsQuery matched;
    qint64 providerId = 0;
    bool instrumental = false;

    bool isSynced() const { return !krcLyrics.isEmpty() || !syncedLyrics.isEmpty(); }
    bool isEmpty() const { return !instrumental && !isSynced() && plainLyrics.trimmed().isEmpty(); }
    void validate();
};

struct LyricsCandidate {
    LyricsDocument document;
    double score = 0;
    QString resolverHandle;
    bool songResult = false;
};

struct LyricsSource {
    QString id;
    QString name;
    bool supportsSearch = true;
    bool staged = false;
};
struct LyricsSnapshot {
    QString trackId;
    quint64 revision = 0;
    QString state;
    QString error;
    bool offline = false;
    QString searchSource;
    QString searchStage;
    bool cached = false;
    bool cacheWarning = false;
    std::optional<LyricsDocument> document;
    QVector<LyricsCandidate> candidates;
};
} // namespace nekotune
Q_DECLARE_METATYPE(nekotune::LyricsSnapshot)
Q_DECLARE_METATYPE(nekotune::LyricsQuery)
Q_DECLARE_METATYPE(nekotune::LyricsDocument)
