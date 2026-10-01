#include "core/player_engine.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QDir>
#include <QSet>
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
        {QStringLiteral("song_id"), metadata.id},           {QStringLiteral("song_hash"), metadata.hash},
        {QStringLiteral("first_path"), metadata.firstPath}, {QStringLiteral("custom_title"), metadata.customTitle},
        {QStringLiteral("artist"), metadata.artist},        {QStringLiteral("lyrics"), metadata.lyrics},
    };
}

bool positiveInteger(const QJsonValue &value)
{
    return value.isDouble() && value.toInt(-1) > 0 && value.toDouble() == value.toInt(-1);
}

QJsonArray tagArray(const QVector<SongTag> &tags)
{
    QJsonArray result;
    for (const auto &tag : tags)
        result.append(QJsonObject{{QStringLiteral("id"), tag.id}, {QStringLiteral("name"), tag.name}});
    return result;
}

QString localCoverUrl(const QString &audioPath)
{
    const QFileInfo audio(audioPath);
    const auto base = QDir(audio.absolutePath()).filePath(audio.completeBaseName());
    for (const auto &suffix : {QStringLiteral(".jpg"), QStringLiteral(".jpeg"),
                               QStringLiteral(".png"), QStringLiteral(".webp")}) {
        const QFileInfo image(base + suffix);
        if (image.isFile()) return QUrl::fromLocalFile(image.absoluteFilePath()).toString();
    }
    return {};
}

} // namespace

PlayerEngine::PlayerEngine(QObject *parent) : QObject(parent)
{
    m_kugouService = new KugouMusicService(this);
    connect(m_kugouService, &KugouMusicService::eventReady, this, &PlayerEngine::eventReady);
    connect(m_kugouService, &KugouMusicService::audioReady, this,
            [this](const QString &path, const QString &lyricStatus, const QString &coverStatus,
                   const QString &title, const QString &artist) {
        const auto imported = importLibrarySong(path, title, artist);
        if (imported.value(QStringLiteral("status")).toString() != QStringLiteral("ok")) {
            emit eventReady({{QStringLiteral("event"), QStringLiteral("kugou.operation_failed")},
                             {QStringLiteral("message"), QStringLiteral("Audio saved but library import failed")},
                             {QStringLiteral("path"), path}});
            return;
        }
        emit eventReady({{QStringLiteral("event"), QStringLiteral("kugou.download_finished")},
                         {QStringLiteral("path"), path},
                         {QStringLiteral("song_id"), imported.value(QStringLiteral("data")).toObject()
                                                          .value(QStringLiteral("song_id"))},
                         {QStringLiteral("lyric_status"), lyricStatus},
                         {QStringLiteral("cover_status"), coverStatus}});
        if (m_queue.currentIndex() >= 0 && m_queue.at(m_queue.currentIndex()).path == path)
            broadcastTrackChanged();
    });
    m_audioOutput.setVolume(0.8);
    m_player.setAudioOutput(&m_audioOutput);

    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, &PlayerEngine::handlePlaybackStateChanged);
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, &PlayerEngine::handleMediaStatusChanged);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, &PlayerEngine::handleErrorChanged);
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

    m_lyricsService = new LyricsService;
    m_lyricsService->moveToThread(&m_lyricsThread);
    connect(&m_lyricsThread, &QThread::finished, m_lyricsService, &QObject::deleteLater);
    connect(m_lyricsService, &LyricsService::changed, this, [this](const QJsonObject &snapshot) {
        if (snapshot.value(QStringLiteral("revision")).toString() != QString::number(m_lyricsRevision) ||
            snapshot.value(QStringLiteral("track_id")).toString() != lyricsQuery().trackId)
            return;
        m_lyrics = snapshot;
        emit eventReady(
            {{QStringLiteral("event"), QStringLiteral("lyrics.changed")}, {QStringLiteral("lyrics"), m_lyrics}});
    });
    m_lyricsThread.start();
    m_metadataTimer.setSingleShot(true);
    m_metadataTimer.setInterval(100);
    connect(&m_metadataTimer, &QTimer::timeout, this, [this]() {
        if (m_metadataReady || m_player.source().isEmpty())
            return;
        const auto status = m_player.mediaStatus();
        if (status != QMediaPlayer::LoadedMedia && status != QMediaPlayer::BufferedMedia &&
            status != QMediaPlayer::BufferingMedia && status != QMediaPlayer::EndOfMedia)
            return;
        m_fileMetadata = m_player.metaData();
        m_metadataReady = true;
        broadcastTrackChanged();
        loadLyrics(true);
    });
    connect(&m_player, &QMediaPlayer::metaDataChanged, &m_metadataTimer, qOverload<>(&QTimer::start));
    connect(&m_player, &QMediaPlayer::durationChanged, &m_metadataTimer, qOverload<>(&QTimer::start));
    restoreQueueFromStore();
    loadLyrics(false);
}

PlayerEngine::~PlayerEngine()
{
    m_lyricsThread.quit();
    m_lyricsThread.wait();
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
        {QStringLiteral("playlists"), playlists()},
        {QStringLiteral("database_path"), m_songStore.databasePath()},
        {QStringLiteral("lyrics"), m_lyrics},
        {QStringLiteral("kugou"), m_kugouService->status()},
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
    broadcastLibraryChanged();
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
    broadcastLibraryChanged();

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

QJsonArray PlayerEngine::playlists() const
{
    QHash<int, SongMetadata> metadata;
    for (const auto &song : m_songStore.songs()) metadata.insert(song.id, song);
    QJsonArray result;
    for (const auto &playlist : m_songStore.playlists()) {
        QJsonArray items;
        for (const auto &record : playlist.items) {
            if (!metadata.contains(record.songId)) continue;
            const auto &song = metadata[record.songId];
            auto item = metadataToObject(song);
            item.remove(QStringLiteral("lyrics"));
            item.insert(QStringLiteral("path"), record.path);
            item.insert(QStringLiteral("title"), song.customTitle.trimmed().isEmpty()
                ? QFileInfo(record.path).completeBaseName() : song.customTitle.trimmed());
            items.append(item);
        }
        result.append(QJsonObject{{QStringLiteral("id"), playlist.id},
                                  {QStringLiteral("name"), playlist.name},
                                  {QStringLiteral("items"), items}});
    }
    return result;
}

void PlayerEngine::broadcastPlaylistsChanged()
{
    emit eventReady({{QStringLiteral("event"), QStringLiteral("playlist.changed")},
                     {QStringLiteral("playlists"), playlists()}});
}

QString PlayerEngine::availablePath(int songId) const
{
    for (const auto &path : m_songStore.pathsForSong(songId)) {
        if (QFileInfo(path).isFile()) return path;
    }
    return {};
}

QJsonObject PlayerEngine::library() const
{
    const auto memberships = m_songStore.songTags();
    const auto paths = m_songStore.songPaths();
    QJsonArray songs;
    for (const auto &song : m_songStore.songs()) {
        auto item = metadataToObject(song);
        item.remove(QStringLiteral("lyrics"));
        QString path;
        for (const auto &candidate : paths.value(song.id)) {
            if (QFileInfo(candidate).isFile()) { path = candidate; break; }
        }
        item.insert(QStringLiteral("path"), path);
        item.insert(QStringLiteral("available"), !path.isEmpty());
        item.insert(QStringLiteral("title"), song.customTitle.trimmed().isEmpty()
            ? QFileInfo(path.isEmpty() ? song.firstPath : path).completeBaseName()
            : song.customTitle.trimmed());
        item.insert(QStringLiteral("tags"), tagArray(memberships.value(song.id)));
        songs.append(item);
    }
    return {{QStringLiteral("songs"), songs}, {QStringLiteral("tags"), tagArray(m_songStore.tags())}};
}

void PlayerEngine::broadcastLibraryChanged()
{
    emit eventReady({{QStringLiteral("event"), QStringLiteral("library.changed")}});
}

QJsonObject PlayerEngine::listLibrary() const
{
    return ok({{QStringLiteral("library"), library()}});
}

QJsonObject PlayerEngine::importLibrarySong(const QString &path, const QString &title, const QString &artist)
{
    const QFileInfo file(path);
    if (!file.isFile()) return error(QStringLiteral("File does not exist: %1").arg(path));
    QString hashError;
    const QString hash = calculateSongHash(file.absoluteFilePath(), &hashError);
    if (hash.isEmpty()) return error(hashError);
    const auto metadata = m_songStore.getOrCreateSong(hash, file.absoluteFilePath(), title, artist);
    if (!metadata)
        return error(QStringLiteral("Unable to store song metadata: %1").arg(m_songStore.errorString()));
    broadcastLibraryChanged();
    return ok({{QStringLiteral("song_id"), metadata->id}, {QStringLiteral("path"), file.absoluteFilePath()}});
}

QJsonObject PlayerEngine::kugouAction(const QString &action, const QJsonObject &params)
{
    if (action == QStringLiteral("status")) return ok({{QStringLiteral("kugou"), m_kugouService->status()}});
    QString problem;
    if (action == QStringLiteral("send_code")) {
        problem = m_kugouService->startCodeRequest(params.value(QStringLiteral("mobile")).toString());
    } else if (action == QStringLiteral("save_key")) {
        if (!params.value(QStringLiteral("key")).isString()) return error(QStringLiteral("key must be a string"));
        problem = m_kugouService->saveAccountKey(params.value(QStringLiteral("key")).toString());
    } else if (action == QStringLiteral("clear_key")) {
        problem = m_kugouService->clearAccountKey();
    } else if (action == QStringLiteral("login")) {
        problem = m_kugouService->startLogin(params.value(QStringLiteral("mobile")).toString(),
                                            params.value(QStringLiteral("code")).toString());
    } else if (action == QStringLiteral("search")) {
        problem = m_kugouService->startSearch(params.value(QStringLiteral("keywords")).toString(),
                                              params.value(QStringLiteral("page")).toInt(1));
    } else if (action == QStringLiteral("download")) {
        problem = m_kugouService->startDownload(params.value(QStringLiteral("hash")).toString());
    } else if (action == QStringLiteral("cancel")) {
        problem = m_kugouService->cancelDownload();
    } else {
        return error(QStringLiteral("Unknown Kugou action"));
    }
    return problem.isEmpty() ? ok({{QStringLiteral("kugou"), m_kugouService->status()}}) : error(problem);
}

QJsonObject PlayerEngine::deleteLibrarySongs(const QJsonObject &params)
{
    const auto value = params.value(QStringLiteral("song_ids"));
    if (!value.isArray() || value.toArray().isEmpty())
        return error(QStringLiteral("song_ids must be a non-empty array"));

    QVector<int> songIds;
    QSet<int> selected;
    for (const auto &entry : value.toArray()) {
        if (!positiveInteger(entry) || selected.contains(entry.toInt()))
            return error(QStringLiteral("Invalid or duplicate song id"));
        const int id = entry.toInt();
        songIds.append(id);
        selected.insert(id);
    }

    const int oldCurrentIndex = m_queue.currentIndex();
    const bool currentDeleted = oldCurrentIndex >= 0
        && selected.contains(m_queue.at(oldCurrentIndex).metadata.id);
    const bool resumePlayback = m_state == PlayerState::Playing || m_state == PlayerState::Loading;
    int nextQueueId = 0;
    if (currentDeleted) {
        for (int index = oldCurrentIndex + 1; index < m_queue.size(); ++index) {
            if (!selected.contains(m_queue.at(index).metadata.id)) {
                nextQueueId = m_queue.at(index).id;
                break;
            }
        }
    }

    PlayerQueue remaining = m_queue;
    for (int index = remaining.size() - 1; index >= 0; --index) {
        if (selected.contains(remaining.at(index).metadata.id)) remaining.removeAt(index);
    }
    if (currentDeleted) remaining.setCurrentIndex(nextQueueId ? remaining.indexById(nextQueueId) : -1);
    remaining.markCurrent();
    if (!m_songStore.deleteSongs(songIds, {remaining.records(), remaining.currentIndex()}))
        return error(m_songStore.errorString());

    if (currentDeleted) {
        m_player.stop();
        m_player.setSource(QUrl());
    }
    m_queue = remaining;
    if (currentDeleted) {
        if (m_queue.currentIndex() >= 0) {
            loadCurrent();
            if (resumePlayback) m_player.play();
        } else {
            setState(PlayerState::Stopped);
            broadcastTrackChanged();
        }
    }
    broadcastQueueChanged();
    broadcastPlaylistsChanged();
    broadcastLibraryChanged();
    return ok({{QStringLiteral("deleted_count"), songIds.size()}});
}

QJsonObject PlayerEngine::manageTag(const QString &action, const QJsonObject &params)
{
    if (action == QStringLiteral("create")) {
        if (!params.value(QStringLiteral("name")).isString()) return error(QStringLiteral("Tag name is required"));
        const int id = m_songStore.createTag(params.value(QStringLiteral("name")).toString());
        if (!id) return error(m_songStore.errorString());
        broadcastLibraryChanged();
        return ok({{QStringLiteral("tag_id"), id}});
    }
    if (!positiveInteger(params.value(QStringLiteral("id")))) return error(QStringLiteral("Invalid tag id"));
    const int id = params.value(QStringLiteral("id")).toInt();
    bool saved = false;
    if (action == QStringLiteral("rename")) {
        if (!params.value(QStringLiteral("name")).isString()) return error(QStringLiteral("Tag name is required"));
        saved = m_songStore.renameTag(id, params.value(QStringLiteral("name")).toString());
    } else if (action == QStringLiteral("delete")) {
        saved = m_songStore.deleteTag(id);
    } else {
        return error(QStringLiteral("Unknown tag action"));
    }
    if (!saved) return error(m_songStore.errorString());
    broadcastLibraryChanged();
    return ok();
}

QJsonObject PlayerEngine::playLibrary(const QJsonObject &params)
{
    const auto tagValue = params.value(QStringLiteral("tag_ids"));
    if (!tagValue.isUndefined() && !tagValue.isArray()) return error(QStringLiteral("tag_ids must be an array"));
    QSet<int> selectedTags;
    for (const auto &value : tagValue.toArray()) {
        if (!positiveInteger(value)) return error(QStringLiteral("Invalid tag id"));
        selectedTags.insert(value.toInt());
    }
    QSet<int> existingTags;
    for (const auto &tag : m_songStore.tags()) existingTags.insert(tag.id);
    for (int id : selectedTags) {
        if (!existingTags.contains(id)) return error(QStringLiteral("Tag does not exist"));
    }
    const auto startValue = params.value(QStringLiteral("song_id"));
    if (!startValue.isUndefined() && !positiveInteger(startValue)) return error(QStringLiteral("Invalid song id"));
    const int requestedSong = startValue.isUndefined() ? 0 : startValue.toInt();

    PlayerQueue queue = m_queue;
    queue.clear();
    QHash<int, SongMetadata> metadataById;
    for (const auto &song : m_songStore.songs()) metadataById.insert(song.id, song);
    QJsonArray skipped;
    int startIndex = requestedSong == 0 ? 0 : -1;
    for (const auto &value : library().value(QStringLiteral("songs")).toArray()) {
        const auto song = value.toObject();
        QSet<int> songTagIds;
        for (const auto &tagValue : song.value(QStringLiteral("tags")).toArray())
            songTagIds.insert(tagValue.toObject().value(QStringLiteral("id")).toInt());
        bool matches = true;
        for (int id : selectedTags) {
            if (!songTagIds.contains(id)) { matches = false; break; }
        }
        if (!matches) continue;
        const int songId = song.value(QStringLiteral("song_id")).toInt();
        const auto path = song.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            skipped.append(songId);
            continue;
        }
        if (!metadataById.contains(songId)) return error(QStringLiteral("Song metadata not found"));
        if (requestedSong == songId) startIndex = queue.size();
        queue.add(path, metadataById.value(songId));
    }
    if (queue.isEmpty()) return error(QStringLiteral("No playable songs match the selected tags"));
    if (startIndex < 0) return error(QStringLiteral("Selected song is unavailable or does not match the tags"));
    queue.setCurrentIndex(startIndex);
    queue.markCurrent();
    if (!m_songStore.saveQueue({queue.records(), startIndex})) return error(m_songStore.errorString());
    m_player.stop();
    m_player.setSource(QUrl());
    m_queue = queue;
    loadCurrent();
    m_player.play();
    broadcastLibraryChanged();
    auto result = status();
    result.insert(QStringLiteral("skipped_song_ids"), skipped);
    return ok(result);
}

QJsonObject PlayerEngine::managePlaylist(const QString &action, const QJsonObject &params)
{
    auto validId = [&](const QString &key) { return positiveInteger(params.value(key)); };
    if (action == QStringLiteral("list"))
        return ok({{QStringLiteral("playlists"), playlists()}});
    if (action == QStringLiteral("create")) {
        const int id = m_songStore.createPlaylist(params.value(QStringLiteral("name")).toString());
        if (!id) return error(m_songStore.errorString());
        broadcastPlaylistsChanged();
        return ok({{QStringLiteral("playlists"), playlists()}, {QStringLiteral("playlist_id"), id}});
    }
    if (!validId(QStringLiteral("id"))) return error(QStringLiteral("Invalid playlist id"));
    const int id = params.value(QStringLiteral("id")).toInt();
    const auto playlist = m_songStore.playlistById(id);
    if (!playlist) return error(QStringLiteral("Playlist does not exist"));

    bool saved = false;
    if (action == QStringLiteral("rename")) {
        saved = m_songStore.renamePlaylist(id, params.value(QStringLiteral("name")).toString());
    } else if (action == QStringLiteral("delete")) {
        saved = m_songStore.deletePlaylist(id);
    } else if (action == QStringLiteral("add")) {
        QueueRecord record;
        if (params.contains(QStringLiteral("song_id"))) {
            if (!validId(QStringLiteral("song_id"))) return error(QStringLiteral("Invalid song id"));
            const int songId = params.value(QStringLiteral("song_id")).toInt();
            if (!m_songStore.songById(songId)) return error(QStringLiteral("Song does not exist"));
            const auto path = availablePath(songId);
            if (path.isEmpty()) return error(QStringLiteral("Song file is unavailable"));
            record = {path, songId};
        } else if (params.contains(QStringLiteral("queue_id"))) {
            if (!validId(QStringLiteral("queue_id"))) return error(QStringLiteral("Invalid queue item id"));
            const int index = m_queue.indexById(params.value(QStringLiteral("queue_id")).toInt());
            if (index < 0) return error(QStringLiteral("Queue item does not exist"));
            const auto &item = m_queue.at(index);
            record = {item.path, item.metadata.id};
        } else {
            const QString path = params.value(QStringLiteral("path")).toString();
            const QFileInfo file(path);
            if (path.isEmpty() || !file.isFile()) return error(QStringLiteral("File does not exist: %1").arg(path));
            QString message;
            const auto hash = calculateSongHash(file.absoluteFilePath(), &message);
            if (hash.isEmpty()) return error(message);
            const auto song = m_songStore.getOrCreateSong(hash, file.absoluteFilePath());
            if (!song) return error(m_songStore.errorString());
            record = {file.absoluteFilePath(), song->id};
        }
        saved = m_songStore.addPlaylistSong(id, record);
    } else if (action == QStringLiteral("remove")) {
        if (!validId(QStringLiteral("song_id"))) return error(QStringLiteral("Invalid song id"));
        saved = m_songStore.removePlaylistSong(id, params.value(QStringLiteral("song_id")).toInt());
    } else if (action == QStringLiteral("play")) {
        if (playlist->items.isEmpty()) return error(QStringLiteral("Playlist is empty"));
        if (params.contains(QStringLiteral("song_id")) && !validId(QStringLiteral("song_id")))
            return error(QStringLiteral("Invalid song id"));
        const int songId = params.value(QStringLiteral("song_id")).toInt();
        int startIndex = songId == 0 ? 0 : -1;
        PlayerQueue queue = m_queue;
        queue.clear();
        for (const auto &record : playlist->items) {
            const auto song = m_songStore.songById(record.songId);
            if (!song || !QFileInfo(record.path).isFile())
                return error(QStringLiteral("Playlist file is unavailable: %1").arg(record.path));
            if (record.songId == songId) startIndex = queue.size();
            queue.add(record.path, *song);
        }
        if (startIndex < 0) return error(QStringLiteral("Song is not in this playlist"));
        queue.setCurrentIndex(startIndex);
        queue.markCurrent();
        // Validate and persist the full replacement before touching current playback.
        if (!m_songStore.saveQueue({queue.records(), startIndex})) return error(m_songStore.errorString());
        m_player.stop();
        m_player.setSource(QUrl());
        m_queue = queue;
        loadCurrent();
        m_player.play();
        broadcastLibraryChanged();
        return ok(status());
    } else {
        return error(QStringLiteral("Unknown playlist action"));
    }
    if (!saved) return error(m_songStore.errorString());
    broadcastPlaylistsChanged();
    broadcastLibraryChanged();
    return ok({{QStringLiteral("playlists"), playlists()}});
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
    broadcastLibraryChanged();
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

    auto result = metadataToObject(*metadata);
    result.insert(QStringLiteral("tags"), tagArray(m_songStore.songTags().value(metadata->id)));
    return ok(result);
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

    std::optional<QStringList> tags;
    if (params.contains(QStringLiteral("tags"))) {
        const auto value = params.value(QStringLiteral("tags"));
        if (!value.isArray()) return error(QStringLiteral("tags must be an array"));
        QStringList names;
        for (const auto &tag : value.toArray()) {
            if (!tag.isString()) return error(QStringLiteral("Each tag must be a string"));
            names.append(tag.toString());
        }
        tags = names;
    }
    const auto updated = m_songStore.updateMetadata(current->id, customTitle, artist, lyrics, tags);
    if (!updated) {
        return error(QStringLiteral("Unable to update song metadata: %1").arg(m_songStore.errorString()));
    }

    m_queue.updateSongMetadata(*updated);
    broadcastPlaylistsChanged();
    broadcastLibraryChanged();
    loadLyrics(m_metadataReady);
    broadcastTrackChanged();
    broadcastQueueChanged();
    auto result = metadataToObject(*updated);
    result.insert(QStringLiteral("tags"), tagArray(m_songStore.songTags().value(updated->id)));
    return ok(result);
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

    if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)
        m_metadataTimer.start();

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
        broadcastLibraryChanged();
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

    m_metadataTimer.stop();
    m_metadataReady = false;
    m_fileMetadata = {};
    loadLyrics(false);
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
    auto song = m_queue.currentSongObject();
    if (song.isEmpty())
        return song;
    const auto query = lyricsQuery();
    song.insert(QStringLiteral("title"), query.title);
    song.insert(QStringLiteral("artist"), query.artist);
    song.insert(QStringLiteral("album"), query.album);
    song.insert(QStringLiteral("cover_url"), localCoverUrl(song.value(QStringLiteral("path")).toString()));
    return song;
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
    if (m_queue.currentIndex() < 0) {
        m_metadataTimer.stop();
        m_fileMetadata = {};
        m_metadataReady = false;
        loadLyrics(false);
    }
    emit eventReady({
        {QStringLiteral("event"), QStringLiteral("player.track_changed")},
        {QStringLiteral("song"), currentSongObject()},
    });
}

void PlayerEngine::restoreQueueFromStore()
{
    const auto snapshot = m_songStore.loadQueue();
    int currentIndex = -1;
    for (int index = 0; index < snapshot.items.size(); ++index) {
        const auto &record = snapshot.items[index];
        const auto metadata = m_songStore.songById(record.songId);
        if (metadata && !record.path.isEmpty()) {
            if (index == snapshot.currentIndex) currentIndex = m_queue.size();
            m_queue.add(record.path, *metadata);
        }
    }
    m_queue.setCurrentIndex(currentIndex);
    m_queue.markCurrent();
}

void PlayerEngine::persistQueue()
{
    m_songStore.saveQueue({m_queue.records(), m_queue.currentIndex()});
}

LyricsQuery PlayerEngine::lyricsQuery() const
{
    if (m_queue.currentIndex() < 0)
        return {};
    const auto &item = m_queue.at(m_queue.currentIndex());
    QString title = item.metadata.customTitle;
    if (title.isEmpty())
        title = m_fileMetadata.stringValue(QMediaMetaData::Title);
    if (title.isEmpty())
        title = QFileInfo(item.path).completeBaseName();
    QString artist = item.metadata.artist;
    if (artist.isEmpty())
        artist = m_fileMetadata.stringValue(QMediaMetaData::ContributingArtist);
    if (artist.isEmpty())
        artist = m_fileMetadata.stringValue(QMediaMetaData::AlbumArtist);
    return {title, artist, m_fileMetadata.stringValue(QMediaMetaData::AlbumTitle),
            m_metadataReady ? m_player.duration() : 0, item.metadata.hash};
}

void PlayerEngine::loadLyrics(bool metadataReady, bool force)
{
    const auto query = lyricsQuery();
    const auto revision = ++m_lyricsRevision;
    const bool offline = m_lyrics.value(QStringLiteral("offline")).toBool();
    m_lyrics = {{QStringLiteral("track_id"), query.trackId},
                {QStringLiteral("revision"), QString::number(revision)},
                {QStringLiteral("state"), query.trackId.isEmpty() ? QStringLiteral("idle") : QStringLiteral("loading")},
                {QStringLiteral("offline"), offline}};
    emit eventReady(
        {{QStringLiteral("event"), QStringLiteral("lyrics.changed")}, {QStringLiteral("lyrics"), m_lyrics}});
    if (query.trackId.isEmpty()) {
        QMetaObject::invokeMethod(m_lyricsService,
                                  [service = m_lyricsService, revision]() { service->clear(revision); });
        return;
    }
    const auto &item = m_queue.at(m_queue.currentIndex());
    QMetaObject::invokeMethod(m_lyricsService, [service = m_lyricsService, query, path = item.path,
                                                custom = item.metadata.lyrics, revision, metadataReady, force]() {
        service->load(query, path, custom, revision, metadataReady, force);
    });
}

bool PlayerEngine::isCurrentLyricsRequest(const QJsonObject &params) const
{
    return !lyricsQuery().trackId.isEmpty() &&
           params.value(QStringLiteral("track_id")).toString() == lyricsQuery().trackId;
}

QJsonObject PlayerEngine::refreshLyrics(const QJsonObject &params)
{
    if (!isCurrentLyricsRequest(params))
        return error(QStringLiteral("Track is no longer current"));
    loadLyrics(m_metadataReady, true);
    return ok();
}

QJsonObject PlayerEngine::searchLyrics(const QJsonObject &params)
{
    if (!isCurrentLyricsRequest(params))
        return error(QStringLiteral("Track is no longer current"));
    auto query = lyricsQuery();
    for (const auto &field : {QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("album")}) {
        if (params.contains(field) && (!params.value(field).isString() || params.value(field).toString().size() > 500))
            return error(QStringLiteral("Search fields must be strings of at most 500 characters"));
    }
    const auto source = params.value(QStringLiteral("source"));
    if (!source.isUndefined() && (!source.isString() || (source.toString() != QStringLiteral("lrclib") &&
                                                      source.toString() != QStringLiteral("kugou"))))
        return error(QStringLiteral("Invalid lyrics source"));
    query.title = params.value(QStringLiteral("title")).toString(query.title).trimmed();
    query.artist = params.value(QStringLiteral("artist")).toString(query.artist).trimmed();
    query.album = params.value(QStringLiteral("album")).toString(query.album).trimmed();
    if (query.title.isEmpty())
        return error(QStringLiteral("Search title is required"));
    m_metadataTimer.stop();
    const auto revision = ++m_lyricsRevision;
    m_lyrics.insert(QStringLiteral("revision"), QString::number(revision));
    QMetaObject::invokeMethod(m_lyricsService,
                              [service = m_lyricsService, query, revision, sourceName = source.toString(QStringLiteral("lrclib"))]() {
                                  service->search(query, revision, sourceName);
                              });
    return ok();
}

QJsonObject PlayerEngine::selectLyrics(const QJsonObject &params)
{
    if (!isCurrentLyricsRequest(params) ||
        params.value(QStringLiteral("revision")).toString() != QString::number(m_lyricsRevision))
        return error(QStringLiteral("Lyrics results are no longer current"));
    const auto index = params.value(QStringLiteral("index"));
    if (!index.isDouble() || index.toInt(-1) < 0 || index.toDouble() != index.toInt() ||
        index.toInt() >= m_lyrics.value(QStringLiteral("candidates")).toArray().size())
        return error(QStringLiteral("Invalid lyrics candidate index"));
    QMetaObject::invokeMethod(m_lyricsService, [service = m_lyricsService, value = index.toInt(),
                                                revision = m_lyricsRevision]() { service->select(value, revision); });
    return ok();
}

QJsonObject PlayerEngine::setLyricsOffline(bool offline)
{
    m_lyrics.insert(QStringLiteral("offline"), offline);
    QMetaObject::invokeMethod(m_lyricsService,
                              [service = m_lyricsService, offline]() { service->setOffline(offline); });
    if (!offline)
        loadLyrics(m_metadataReady);
    return ok();
}

} // namespace nekotune
