#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>
#include <optional>

namespace nekotune {

/// Parsed LRC lines contain absolute playback timestamps in milliseconds.
/// Text-only metadata tags and empty timestamp markers are filtered out.
/// Equal timestamps retain source order through the parser's stable sort.
/// The frontend's upper-bound lookup selects the last eligible line.
struct LyricLine {
    qint64 timestampMs = -1;
    QString text;
};

/// durationMs == 0 means unknown; trackId is the audio hash, independent of editable display metadata.
struct LyricsQuery {
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0;
    QString trackId;
};

/// May carry KRC, LRC or plain text; instrumental is a valid result even without lyric text.
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

/// resolverHandle is provider-private resolution data; songResult denotes the song-selection stage.
struct LyricsCandidate {
    LyricsDocument document;
    double score = 0;
    QString resolverHandle;
    bool songResult = false;
};

/// id is the stable provider registration/API identifier.
/// name is a display label rather than a resolver credential.
/// supportsSearch determines whether manual search is offered.
/// staged means choosing a song can produce another candidate list.
/// The service handles that progression before applying a document.
struct LyricsSource {
    QString id;
    QString name;
    bool supportsSearch = true;
    bool staged = false;
};
/// trackId and revision jointly identify the current UI request.
/// state distinguishes waiting, searching, candidates and ready data.
/// Candidates may describe song versions rather than downloadable lyrics.
/// searchStage makes that difference explicit for staged providers.
/// document can remain visible while a manual search is in progress.
/// cached records that the current document came from persistent cache.
/// cacheWarning reports persistence failure without hiding valid lyrics.
/// offline limits network work rather than disabling local display.
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
Q_DECLARE_METATYPE(nekotune::LyricsSource)
