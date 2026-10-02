#include "application/playback/player_engine.h"
#include <QFileInfo>
#include <cmath>
namespace nekotune {
PlayerEngine::PlayerEngine(IPlaybackBackend &backend, QueueService &queue,
                           std::unique_ptr<IShuffleStrategy> strategy, PlaybackMode mode,
                           PlaybackOrderService::ModeSaver saveMode)
    : m_backend(backend), m_queue(queue), m_order(std::move(strategy), mode, std::move(saveMode)) {
    m_order.reset(queue.queue());
    connect(&queue, &QueueService::changed, this, [this] {
        if (!m_navigating)
            m_order.syncQueue(m_queue.queue());
    });
    connect(&backend, &IPlaybackBackend::stateChanged, this, &PlayerEngine::setState);
    connect(&backend, &IPlaybackBackend::positionChanged, this,
            [this](qint64 position) { emit positionChanged(position, m_backend.duration()); });
    connect(&backend, &IPlaybackBackend::durationChanged, this, [this](qint64 duration) {
        emit durationChanged(duration);
        m_metadataTimer.start();
    });
    connect(&backend, &IPlaybackBackend::metadataChanged, &m_metadataTimer, qOverload<>(&QTimer::start));
    connect(&backend, &IPlaybackBackend::ended, this, [this] {
        auto result = advance(PlaybackAdvance::Ended);
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
    // Qt reports duration and tags separately; coalesce them before the online lyric lookup.
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
// Merge decoder tags with user-controlled library fields for display.
// The queue occurrence supplies audio identity and the chosen path.
// Custom title and artist take precedence when they are available.
// The original source name is preferable to a numbered managed path.
// This merge does not write decoder tags back into the library.
PlaybackSnapshot PlayerEngine::snapshot() const {
    PlaybackSnapshot result{
        m_state, m_backend.position(), m_backend.duration(), m_backend.volume(), {}, m_metadata};
    result.playbackMode = m_order.mode();
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
    // Local lyrics can load before decoder tags arrive; online matching waits for the timer.
    emit lyricsNeeded(false);
    m_backend.setSource(QUrl::fromLocalFile(queue.at(queue.currentIndex()).path));
    emit trackChanged();
    if (play)
        m_backend.play();
}
void PlayerEngine::clearSource() {
    m_metadataTimer.stop();
    m_backend.stop();
    // stop() alone leaves the old source loaded, allowing play() to revive a removed track.
    m_backend.setSource({});
    m_metadata = {};
    m_metadataReady = false;
    setState(PlayerState::Stopped);
    emit trackChanged();
    emit lyricsNeeded(false);
}
// Restart/resume depends on both queue selection and decoder source.
// A restored queue can have selection without a loaded media source.
// An unselected nonempty queue chooses its first item durably first.
// An already loaded source resumes through the playback adapter.
// An empty queue fails instead of reviving the last cleared source.
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
Result<void> PlayerEngine::setPlaybackMode(PlaybackMode mode) {
    const auto previous = m_order.mode();
    auto result = m_order.setMode(mode, m_queue.queue());
    if (result && previous != mode)
        emit playbackModeChanged(mode);
    return result;
}
Result<void> PlayerEngine::next() { return advance(PlaybackAdvance::Next); }
Result<void> PlayerEngine::previous() { return advance(PlaybackAdvance::Previous); }
Result<void> PlayerEngine::advance(PlaybackAdvance reason) {
    auto queue = m_queue.queue();
    if (queue.isEmpty())
        return reason == PlaybackAdvance::Ended ? Result<void>{} : failure(QStringLiteral("Queue is empty"));
    auto selection = m_order.propose(queue, reason);
    if (!selection.queueId)
        return stop();
    const int index = queue.indexById(selection.queueId);
    if (index < 0)
        return failure(QStringLiteral("Playback order selected an unavailable queue item"));
    queue.setCurrentIndex(index);
    // Queue notifications must not interpret this pending navigation as an external selection.
    m_navigating = true;
    auto saved = m_queue.commit(std::move(queue));
    m_navigating = false;
    if (!saved)
        return saved;
    // Only a durable selection may consume the shuffle proposal and its playback history.
    m_order.confirm(std::move(selection));
    m_backend.stop();
    loadCurrent(true);
    return {};
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
    // Keep the audible track and navigation state intact if persistence fails.
    auto saved = m_queue.commit(std::move(queue));
    if (!saved)
        return saved;
    m_order.reset(m_queue.queue());
    m_backend.stop();
    loadCurrent(play);
    return {};
}
Result<void> PlayerEngine::clear() {
    auto queue = m_queue.queue();
    queue.clear();
    auto result = m_queue.commit(queue);
    if (result) {
        m_order.reset(m_queue.queue());
        clearSource();
    }
    return result;
}
// Removing a noncurrent occurrence must not interrupt playback.
// Removing the current occurrence reloads a surviving successor only
// after the candidate queue has been persisted successfully.
// The captured play state decides whether that successor resumes.
// At the list tail, no successor means clearing the media source.
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
void PlayerEngine::applyCommittedQueue(PlayerQueue queue, bool currentChanged, bool resume, bool resetOrder) {
    m_queue.adoptCommitted(std::move(queue));
    if (resetOrder)
        m_order.reset(m_queue.queue());
    if (currentChanged) {
        m_backend.stop();
        loadCurrent(resume);
    }
}
// A library edit applies to every queued occurrence of that song.
// Only editing the current song needs a playback display refresh.
// Lyrics are reloaded using the decoder's current readiness flag.
// The edit does not seek, switch occurrence or reset shuffle history.
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
