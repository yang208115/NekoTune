#pragma once

#include "core/player_queue.h"
#include "core/player_state.h"
#include "lyrics/lyrics_service.h"
#include "kugou/kugou_music_service.h"
#include "storage/song_store.h"

#include <QAudioOutput>
#include <QJsonArray>
#include <QJsonObject>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QObject>
#include <QThread>
#include <QTimer>

namespace nekotune {

class PlayerEngine final : public QObject {
    Q_OBJECT

public:
    explicit PlayerEngine(QObject *parent = nullptr);
    ~PlayerEngine() override;

    QJsonObject status() const;
    QJsonObject play(const QJsonObject &params);
    QJsonObject togglePlayPause();
    QJsonObject pause();
    QJsonObject stop();
    QJsonObject next();
    QJsonObject previous();
    QJsonObject seek(qint64 positionMs);
    QJsonObject setVolume(double volume);
    QJsonObject addToQueue(const QString &path);
    QJsonObject playQueueItem(int queueId);
    QJsonObject removeFromQueue(int queueId);
    QJsonObject managePlaylist(const QString &action, const QJsonObject &params);
    QJsonObject manageTag(const QString &action, const QJsonObject &params);
    QJsonObject listLibrary() const;
    QJsonObject importLibrarySong(const QString &path, const QString &title = {}, const QString &artist = {});
    QJsonObject kugouAction(const QString &action, const QJsonObject &params);
    QJsonObject deleteLibrarySongs(const QJsonObject &params);
    QJsonObject playLibrary(const QJsonObject &params);
    QJsonArray playlists() const;
    QJsonObject clearQueue();
    QJsonObject queueStatus() const;
    QJsonObject songMetadata(const QJsonObject &params) const;
    QJsonObject updateSongMetadata(const QJsonObject &params);
    QJsonObject refreshLyrics(const QJsonObject &params);
    QJsonObject searchLyrics(const QJsonObject &params);
    QJsonObject selectLyrics(const QJsonObject &params);
    QJsonObject setLyricsOffline(bool offline);

signals:
    void eventReady(const QJsonObject &event);

private slots:
    void handlePlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void handleMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void handleErrorChanged();

private:
    void setState(PlayerState state);
    bool loadCurrent();
    bool playQueueIndex(int index);
    QJsonObject ok(const QJsonObject &data = {}) const;
    QJsonObject error(const QString &message) const;
    QJsonObject currentSongObject() const;
    QJsonArray queueArray() const;
    QJsonObject library() const;
    QString availablePath(int songId) const;
    void broadcastQueueChanged();
    void broadcastPlaylistsChanged();
    void broadcastLibraryChanged();
    void broadcastTrackChanged();
    void restoreQueueFromStore();
    void persistQueue();
    LyricsQuery lyricsQuery() const;
    void loadLyrics(bool metadataReady, bool force = false);
    bool isCurrentLyricsRequest(const QJsonObject &params) const;

    QMediaPlayer m_player;
    QAudioOutput m_audioOutput;
    SongStore m_songStore;
    PlayerQueue m_queue;
    QThread m_lyricsThread;
    LyricsService *m_lyricsService = nullptr;
    KugouMusicService *m_kugouService = nullptr;
    QTimer m_metadataTimer;
    QMediaMetaData m_fileMetadata;
    QJsonObject m_lyrics;
    quint64 m_lyricsRevision = 0;
    bool m_metadataReady = false;
    PlayerState m_state = PlayerState::Stopped;
};

} // namespace nekotune
