#pragma once

#include "core/player_queue.h"
#include "core/player_state.h"
#include "storage/song_store.h"

#include <QAudioOutput>
#include <QJsonArray>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QObject>
#include <QStringList>

namespace nekotune {

class PlayerEngine final : public QObject {
    Q_OBJECT

public:
    explicit PlayerEngine(QObject *parent = nullptr);

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
    QJsonObject clearQueue();
    QJsonObject queueStatus() const;
    QJsonObject songMetadata(const QJsonObject &params) const;
    QJsonObject updateSongMetadata(const QJsonObject &params);

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
    void broadcastQueueChanged();
    void broadcastTrackChanged();
    void restoreQueueFromStore();

    QMediaPlayer m_player;
    QAudioOutput m_audioOutput;
    SongStore m_songStore;
    PlayerQueue m_queue;
    PlayerState m_state = PlayerState::Stopped;
};

} // namespace nekotune
