#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

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
    QString syncedLyrics;
    QString plainLyrics;
    LyricsQuery matched;
    qint64 providerId = 0;
    bool instrumental = false;

    bool isSynced() const
    {
        return !syncedLyrics.isEmpty();
    }
    bool isEmpty() const
    {
        return !instrumental && syncedLyrics.isEmpty() && plainLyrics.trimmed().isEmpty();
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
