#include "core/player_engine.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUrl>

namespace nekotune {

namespace {

constexpr qint64 kPreviousRestartPositionMs = 3000;

QString calculateSongHash(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to read file: %1").arg(path);
        }
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Unable to read file: %1").arg(path);
            }
            return {};
        }
        hash.addData(chunk);
    }

    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject metadataToObject(const SongMetadata &metadata)
{
    return {
        {QStringLiteral("song_id"), metadata.id},
        {QStringLiteral("song_hash"), metadata.hash},
        {QStringLiteral("first_path"), metadata.firstPath},
        {QStringLiteral("custom_title"), metadata.customTitle},
        {QStringLiteral("artist"), metadata.artist},
        {QStringLiteral("lyrics"), metadata.lyrics},
    };
}

} // namespace

PlayerEngine::PlayerEngine(QObject *parent)
    : QObject(parent)
{
    m_audioOutput.setVolume(0.8);
    m_player.setAudioOutput(&m_audioOutput);

    connect(&m_player, &QMediaPlayer::playbackStateChanged,
            this, &PlayerEngine::handlePlaybackStateChanged);
    connect(&m_player, &QMediaPlayer::mediaStatusChanged,
            this, &PlayerEngine::handleMediaStatusChanged);
    connect(&m_player, &QMediaPlayer::errorOccurred,
            this, &PlayerEngine::handleErrorChanged);
    connect(&m_player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        emit eventReady({
            {QStringLiteral("event"), QStringLiteral("player.position_changed")},
            {QStringLiteral("position"), position},
            {QStringLiteral("duration"), m_player.duration()},
        });
    });
    connect(&m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        emit eventReady({
            {QStringLiteral("event"), QStringLiteral("player.duration_changed")},
            {QStringLiteral("duration"), duration},
        });
    });

    restoreQueueFromStore();
}

QJsonObject PlayerEngine::status() const
{
    return {
        {QStringLiteral("state"), toString(m_state)},
        {QStringLiteral("position"), m_player.position()},
        {QStringLiteral("duration"), m_player.duration()},
        {QStringLiteral("volume"), m_audioOutput.volume()},
        {QStringLiteral("song"), currentSongObject()},
        {QStringLiteral("queue"), queueArray()},
        {QStringLiteral("database_path"), m_songStore.databasePath()},
    };
}

QJsonObject PlayerEngine::play(const QJsonObject &params)
{
    const auto path = params.value(QStringLiteral("path")).toString();
    if (!path.isEmpty()) {
        const auto addResult = addToQueue(path);
        if (addResult.value(QStringLiteral("status")).toString() != QStringLiteral("ok")) {
            return addResult;
        }
        return playQueueIndex(m_queue.size() - 1) ? ok(status()) : error(QStringLiteral("Unable to play file"));
    }

    if (m_queue.currentIndex() < 0 && !m_queue.isEmpty()) {
        return playQueueIndex(0) ? ok(status()) : error(QStringLiteral("Unable to play queued file"));
    }

    if (m_player.source().isEmpty()) {
        if (!loadCurrent()) {
            return error(QStringLiteral("No song loaded"));
        }
    }

    m_player.play();
    return ok(status());
}

QJsonObject PlayerEngine::togglePlayPause()
{
    if (m_player.playbackState() == QMediaPlayer::PlayingState) {
        return pause();
    }

    return play({});
}

QJsonObject PlayerEngine::pause()
{
    m_player.pause();
    return ok(status());
}

QJsonObject PlayerEngine::stop()
{
    m_player.stop();
    return ok(status());
}

QJsonObject PlayerEngine::next()
{
    if (m_queue.isEmpty()) {
        return error(QStringLiteral("Queue is empty"));
    }

    const int nextIndex = m_queue.currentIndex() + 1;
    if (nextIndex >= m_queue.size()) {
        m_player.stop();
        setState(PlayerState::Stopped);
        return ok(status());
    }

    return playQueueIndex(nextIndex) ? ok(status()) : error(QStringLiteral("Unable to play next song"));
}

QJsonObject PlayerEngine::previous()
{
    if (m_queue.isEmpty()) {
        return error(QStringLiteral("Queue is empty"));
    }

    if (m_queue.currentIndex() < 0) {
        return playQueueIndex(0) ? ok(status()) : error(QStringLiteral("Unable to play previous song"));
    }

    if (m_player.position() > kPreviousRestartPositionMs) {
        m_player.setPosition(0);
        return ok(status());
    }

    const int previousIndex = m_queue.currentIndex() <= 0 ? 0 : m_queue.currentIndex() - 1;
    return playQueueIndex(previousIndex) ? ok(status()) : error(QStringLiteral("Unable to play previous song"));
}

QJsonObject PlayerEngine::seek(qint64 positionMs)
{
    if (positionMs < 0) {
        return error(QStringLiteral("Position must be greater than or equal to 0"));
    }

    m_player.setPosition(positionMs);
    return ok(status());
}

QJsonObject PlayerEngine::setVolume(double volume)
{
    if (volume < 0.0 || volume > 1.0) {
        return error(QStringLiteral("Volume must be between 0 and 1"));
    }

    m_audioOutput.setVolume(static_cast<float>(volume));
    emit eventReady({
        {QStringLiteral("event"), QStringLiteral("player.volume_changed")},
        {QStringLiteral("volume"), m_audioOutput.volume()},
    });
    return ok(status());
}

QJsonObject PlayerEngine::addToQueue(const QString &path)
{
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile()) {
        return error(QStringLiteral("File does not exist: %1").arg(path));
    }

    QString hashError;
    const QString songHash = calculateSongHash(file.absoluteFilePath(), &hashError);
    if (songHash.isEmpty()) {
        return error(hashError);
    }

    const auto metadata = m_songStore.getOrCreateSong(songHash, file.absoluteFilePath());
    if (!metadata) {
        return error(QStringLiteral("Unable to store song metadata: %1").arg(m_songStore.errorString()));
    }

    m_queue.add(file.absoluteFilePath(), *metadata);
    persistQueue();
    broadcastQueueChanged();
    return ok(queueStatus());
}

QJsonObject PlayerEngine::playQueueItem(int queueId)
{
    const int index = m_queue.indexById(queueId);
    if (index < 0) {
        return error(QStringLiteral("Queue item not found: %1").arg(queueId));
    }

    return playQueueIndex(index) ? ok(status()) : error(QStringLiteral("Unable to play queued song"));
}

QJsonObject PlayerEngine::removeFromQueue(int queueId)
{
    const int index = m_queue.indexById(queueId);
    if (index < 0) {
        return error(QStringLiteral("Queue item not found: %1").arg(queueId));
    }

    const int currentIndex = m_queue.currentIndex();
    const bool removingCurrent = index == currentIndex;
    const bool shouldContinue = m_state == PlayerState::Playing || m_state == PlayerState::Loading;

    m_queue.removeAt(index);
    persistQueue();

    if (removingCurrent) {
        m_player.stop();

        if (m_queue.isEmpty()) {
            m_queue.setCurrentIndex(-1);
            setState(PlayerState::Stopped);
            broadcastTrackChanged();
            broadcastQueueChanged();
            return ok(queueStatus());
        }

        if (index >= m_queue.size()) {
            m_queue.setCurrentIndex(-1);
            setState(PlayerState::Stopped);
            broadcastTrackChanged();
            broadcastQueueChanged();
            return ok(queueStatus());
        }

        m_queue.setCurrentIndex(index);
        if (!loadCurrent()) {
            return error(QStringLiteral("Unable to load next queued song"));
        }
        if (shouldContinue) {
            m_player.play();
        }
        persistQueue();
        return ok(status());
    }

    broadcastQueueChanged();
    return ok(queueStatus());
}

QJsonObject PlayerEngine::clearQueue()
{
    m_player.stop();
    m_queue.clear();
    m_player.setSource(QUrl());
    persistQueue();
    setState(PlayerState::Stopped);
    broadcastTrackChanged();
    broadcastQueueChanged();
    return ok(queueStatus());
}

QJsonObject PlayerEngine::queueStatus() const
{
    return {
        {QStringLiteral("current_index"), m_queue.currentIndex()},
        {QStringLiteral("items"), queueArray()},
    };
}

QJsonObject PlayerEngine::songMetadata(const QJsonObject &params) const
{
    const int songId = params.value(QStringLiteral("song_id")).toInt();
    const int fallbackId = params.value(QStringLiteral("id")).toInt();
    const auto metadata = m_songStore.songById(songId > 0 ? songId : fallbackId);
    if (!metadata) {
        return error(QStringLiteral("Song metadata not found"));
    }

    return ok(metadataToObject(*metadata));
}

QJsonObject PlayerEngine::updateSongMetadata(const QJsonObject &params)
{
    const int songId = params.value(QStringLiteral("song_id")).toInt();
    const int fallbackId = params.value(QStringLiteral("id")).toInt();
    const auto current = m_songStore.songById(songId > 0 ? songId : fallbackId);
    if (!current) {
        return error(QStringLiteral("Song metadata not found"));
    }

    QString customTitle = current->customTitle;
    if (params.contains(QStringLiteral("custom_title"))) {
        customTitle = params.value(QStringLiteral("custom_title")).toString();
    } else if (params.contains(QStringLiteral("title"))) {
        customTitle = params.value(QStringLiteral("title")).toString();
    }

    QString artist = current->artist;
    if (params.contains(QStringLiteral("artist"))) {
        artist = params.value(QStringLiteral("artist")).toString();
    } else if (params.contains(QStringLiteral("author"))) {
        artist = params.value(QStringLiteral("author")).toString();
    }

    QString lyrics = current->lyrics;
    if (params.contains(QStringLiteral("lyrics"))) {
        lyrics = params.value(QStringLiteral("lyrics")).toString();
    }

    const auto updated = m_songStore.updateMetadata(current->id, customTitle, artist, lyrics);
    if (!updated) {
        return error(QStringLiteral("Unable to update song metadata: %1").arg(m_songStore.errorString()));
    }

    m_queue.updateSongMetadata(*updated);
    broadcastTrackChanged();
    broadcastQueueChanged();
    return ok(metadataToObject(*updated));
}

void PlayerEngine::handlePlaybackStateChanged(QMediaPlayer::PlaybackState state)
{
    switch (state) {
    case QMediaPlayer::StoppedState:
        setState(PlayerState::Stopped);
        break;
    case QMediaPlayer::PlayingState:
        setState(PlayerState::Playing);
        break;
    case QMediaPlayer::PausedState:
        setState(PlayerState::Paused);
        break;
    }
}

void PlayerEngine::handleMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::LoadingMedia || status == QMediaPlayer::BufferingMedia) {
        if (m_player.playbackState() == QMediaPlayer::PlayingState) {
            return;
        }
        setState(PlayerState::Loading);
        return;
    }

    if (status == QMediaPlayer::EndOfMedia) {
        next();
    }
}

void PlayerEngine::handleErrorChanged()
{
    if (m_player.error() == QMediaPlayer::NoError) {
        return;
    }

    setState(PlayerState::Error);
    const int failedIndex = m_queue.currentIndex();
    if (failedIndex >= 0 && failedIndex < m_queue.size()) {
        m_queue.removeAt(failedIndex);
        m_player.setSource(QUrl());
        persistQueue();
        broadcastTrackChanged();
        broadcastQueueChanged();
    }
    emit eventReady({
        {QStringLiteral("event"), QStringLiteral("player.error")},
        {QStringLiteral("message"), m_player.errorString()},
    });
}

void PlayerEngine::setState(PlayerState state)
{
    if (m_state == state) {
        return;
    }

    m_state = state;
    emit eventReady({
        {QStringLiteral("event"), QStringLiteral("player.state_changed")},
        {QStringLiteral("state"), toString(m_state)},
    });
}

bool PlayerEngine::loadCurrent()
{
    const int currentIndex = m_queue.currentIndex();
    if (currentIndex < 0 || currentIndex >= m_queue.size()) {
        return false;
    }

    m_queue.markCurrent();

    m_player.setSource(QUrl::fromLocalFile(m_queue.at(currentIndex).path));
    broadcastTrackChanged();
    broadcastQueueChanged();
    return true;
}

bool PlayerEngine::playQueueIndex(int index)
{
    if (index < 0 || index >= m_queue.size()) {
        return false;
    }

    m_queue.setCurrentIndex(index);
    persistQueue();
    if (!loadCurrent()) {
        return false;
    }

    m_player.play();
    return true;
}

QJsonObject PlayerEngine::ok(const QJsonObject &data) const
{
    return {
        {QStringLiteral("status"), QStringLiteral("ok")},
        {QStringLiteral("data"), data},
    };
}

QJsonObject PlayerEngine::error(const QString &message) const
{
    return {
        {QStringLiteral("status"), QStringLiteral("error")},
        {QStringLiteral("message"), message},
    };
}

QJsonObject PlayerEngine::currentSongObject() const
{
    return m_queue.currentSongObject();
}

QJsonArray PlayerEngine::queueArray() const
{
    return m_queue.toArray();
}

void PlayerEngine::broadcastQueueChanged()
{
    emit eventReady({
        {QStringLiteral("event"), QStringLiteral("queue.changed")},
        {QStringLiteral("queue"), queueArray()},
    });
}

void PlayerEngine::broadcastTrackChanged()
{
    emit eventReady({
        {QStringLiteral("event"), QStringLiteral("player.track_changed")},
        {QStringLiteral("song"), currentSongObject()},
    });
}

void PlayerEngine::restoreQueueFromStore()
{
    const auto snapshot = m_songStore.loadQueue();
    for (const auto &record : snapshot.items) {
        const auto metadata = m_songStore.songById(record.songId);
        if (metadata && !record.path.isEmpty()) {
            m_queue.add(record.path, *metadata);
        }
    }

    if (!m_queue.isEmpty()) {
        m_queue.setCurrentIndex(qBound(0, snapshot.currentIndex, m_queue.size() - 1));
        m_queue.markCurrent();
    }
}

void PlayerEngine::persistQueue()
{
    m_songStore.saveQueue({m_queue.records(), m_queue.currentIndex()});
}

} // namespace nekotune
