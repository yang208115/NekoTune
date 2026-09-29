#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace nekotune {

struct LyricWord {
    qint64 timestampMs = -1;
    qint64 endTimestampMs = -1;
    qsizetype start = 0;
    qsizetype length = 0;
};

struct LyricLine {
    qint64 timestampMs = -1;
    QString text;
    qint64 endTimestampMs = -1;
    QVector<LyricWord> words;
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
    QString syncedLyrics;
    QString plainLyrics;
    QString asrLyrics;
    LyricsQuery matched;
    qint64 providerId = 0;
    bool instrumental = false;

    bool isSynced() const
    {
        return !syncedLyrics.isEmpty() || !asrLyrics.isEmpty();
    }
    bool isEmpty() const
    {
        return !instrumental && !isSynced() && plainLyrics.trimmed().isEmpty();
    }
    void validate();
    QJsonObject toJson() const;
};

struct LyricsCandidate {
    LyricsDocument document;
    double score = 0;
    QJsonObject toJson() const;
};

} // namespace nekotune
