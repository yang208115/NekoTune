#include "core/player_engine.h"
#include <QFileInfo>
#include <cmath>
namespace nekotune {
PlayerEngine::PlayerEngine(IPlaybackBackend &backend, QueueService &queue)
    : m_backend(backend), m_queue(queue) {
    connect(&backend, &IPlaybackBackend::stateChanged, this, &PlayerEngine::setState);
    connect(&backend, &IPlaybackBackend::positionChanged, this,
            [this](qint64 position) { emit positionChanged(position, m_backend.duration()); });
    connect(&backend, &IPlaybackBackend::durationChanged, this, [this](qint64 duration) {
        emit durationChanged(duration);
        m_metadataTimer.start();
    });
    connect(&backend, &IPlaybackBackend::metadataChanged, &m_metadataTimer, qOverload<>(&QTimer::start));
    connect(&backend, &IPlaybackBackend::ended, this, [this] {
        auto result = next();
        if (!result)
            emit errorOccurred(result.error().message);
    });
    connect(&backend, &IPlaybackBackend::failed, this, [this](const QString &message) {
        setState(PlayerState::Error);
        m_backend.setSource({});
        auto next = m_queue.queue();
        if (next.currentIndex() >= 0)
            next.removeAt(next.currentIndex());
        auto result = m_queue.commit(next);
        clearSource();
        setState(PlayerState::Error);
        emit errorOccurred(result ? message : message + QStringLiteral("; ") + result.error().message);
    });
    m_metadataTimer.setSingleShot(true);
    m_metadataTimer.setInterval(100);
    connect(&m_metadataTimer, &QTimer::timeout, this, [this] {
        if (m_backend.source().isEmpty() || m_metadataReady)
            return;
        m_metadata = m_backend.metadata();
        m_metadataReady = true;
        emit trackChanged();
        emit lyricsNeeded(true);
    });
}
PlaybackSnapshot PlayerEngine::snapshot() const {
    PlaybackSnapshot result{
        m_state, m_backend.position(), m_backend.duration(), m_backend.volume(), {}, m_metadata};
    const auto &queue = m_queue.queue();
    if (queue.currentIndex() < 0)
        return result;
    result.song = queue.at(queue.currentIndex());
    const auto &song = *result.song;
    if (!song.metadata.customTitle.isEmpty())
        result.metadata.title = song.metadata.customTitle;
    if (result.metadata.title.isEmpty())
        result.metadata.title = song.metadata.sourceName.isEmpty() ? QFileInfo(song.path).completeBaseName() : song.metadata.sourceName;
    if (!song.metadata.artist.isEmpty())
        result.metadata.artist = song.metadata.artist;
    return result;
}
void PlayerEngine::setState(PlayerState state) {
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}
void PlayerEngine::loadCurrent(bool play) {
    const auto &queue = m_queue.queue();
    if (queue.currentIndex() < 0) {
        clearSource();
        return;
    }
    m_metadataTimer.stop();
    m_metadata = {};
    m_metadataReady = false;
    emit lyricsNeeded(false);
    m_backend.setSource(QUrl::fromLocalFile(queue.at(queue.currentIndex()).path));
    emit trackChanged();
    if (play)
        m_backend.play();
}
void PlayerEngine::clearSource() {
    m_metadataTimer.stop();
    m_backend.stop();
    m_backend.setSource({});
    m_metadata = {};
    m_metadataReady = false;
    setState(PlayerState::Stopped);
    emit trackChanged();
    emit lyricsNeeded(false);
}
Result<void> PlayerEngine::play() {
    auto queue = m_queue.queue();
    if (queue.isEmpty())
        return failure(QStringLiteral("No song loaded"));
    if (queue.currentIndex() < 0) {
        queue.setCurrentIndex(0);
        return replaceQueue(queue);
    }
    if (m_backend.source().isEmpty())
        loadCurrent(true);
    else
        m_backend.play();
    return {};
}
Result<void> PlayerEngine::toggle() { return playing() ? pause() : play(); }
Result<void> PlayerEngine::pause() {
    m_backend.pause();
    return {};
}
Result<void> PlayerEngine::stop() {
    m_backend.stop();
    return {};
}
Result<void> PlayerEngine::next() {
    const auto &queue = m_queue.queue();
    if (queue.isEmpty())
        return failure(QStringLiteral("Queue is empty"));
    int index = queue.currentIndex() + 1;
    if (index >= queue.size())
        return stop();
    return playItem(queue.at(index).id);
}
Result<void> PlayerEngine::previous() {
    const auto &queue = m_queue.queue();
    if (queue.isEmpty())
        return failure(QStringLiteral("Queue is empty"));
    if (m_backend.position() > 3000)
        return seek(0);
    return playItem(queue.at(qMax(0, queue.currentIndex() - 1)).id);
}
Result<void> PlayerEngine::seek(qint64 position) {
    if (position < 0)
        return failure(QStringLiteral("Position must be greater than or equal to 0"));
    m_backend.seek(position);
    return {};
}
Result<void> PlayerEngine::setVolume(double volume) {
    if (!std::isfinite(volume) || volume < 0 || volume > 1)
        return failure(QStringLiteral("Volume must be between 0 and 1"));
    m_backend.setVolume(volume);
    emit volumeChanged(m_backend.volume());
    return {};
}
Result<void> PlayerEngine::playItem(int id) {
    auto queue = m_queue.queue();
    int index = queue.indexById(id);
    if (index < 0)
        return failure(QStringLiteral("Queue item not found: %1").arg(id));
    queue.setCurrentIndex(index);
    return replaceQueue(queue);
}
Result<void> PlayerEngine::replaceQueue(PlayerQueue queue, bool play) {
    auto saved = m_queue.commit(std::move(queue));
    if (!saved)
        return saved;
    m_backend.stop();
    loadCurrent(play);
    return {};
}
Result<void> PlayerEngine::clear() {
    auto queue = m_queue.queue();
    queue.clear();
    auto result = m_queue.commit(queue);
    if (result)
        clearSource();
    return result;
}
Result<void> PlayerEngine::removeItem(int id) {
    auto queue = m_queue.queue();
    int index = queue.indexById(id);
    if (index < 0)
        return failure(QStringLiteral("Queue item not found: %1").arg(id));
    const bool current = index == queue.currentIndex(), resume = playing();
    queue.removeAt(index);
    if (current && index < queue.size())
        queue.setCurrentIndex(index);
    auto result = m_queue.commit(queue);
    if (!result)
        return result;
    if (current) {
        m_backend.stop();
        loadCurrent(resume);
    }
    return {};
}
void PlayerEngine::applyCommittedQueue(PlayerQueue queue, bool currentChanged, bool resume) {
    m_queue.adoptCommitted(std::move(queue));
    if (currentChanged) {
        m_backend.stop();
        loadCurrent(resume);
    }
}
void PlayerEngine::metadataUpdated(const SongMetadata &metadata) {
    m_queue.updateMetadata(metadata);
    auto state = snapshot();
    if (state.song && state.song->metadata.id == metadata.id) {
        emit trackChanged();
        emit lyricsNeeded(m_metadataReady);
    }
}
void PlayerEngine::shutdown() {
    m_metadataTimer.stop();
    m_backend.stop();
    m_backend.setSource({});
}
} // namespace nekotune
