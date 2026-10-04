#include "application/playback/player_engine.h"
#include <QFileInfo>
#include <QPointer>
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
        const auto &current = m_queue.queue();
        if (current.currentIndex() >= 0 && current.at(current.currentIndex()).metadata.isRemote()) {
            ++m_sourceGeneration;
            m_resolving = false;
            m_playIntent = false;
            m_backend.setSource({});
            if (m_sourceResolver)
                m_sourceResolver->release(m_resolvedSource);
            m_resolvedSource = QUrl{};
            setState(PlayerState::Error);
            emit errorOccurred(message);
            return;
        }
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
    if (!song.metadata.album.isEmpty())
        result.metadata.album = song.metadata.album;
    return result;
}
void PlayerEngine::setState(PlayerState state) {
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}
void PlayerEngine::loadCurrent(bool play) {
    const auto generation = ++m_sourceGeneration;
    m_resolving = false;
    m_waitingForReload = false;
    m_playIntent = play;
    if (m_sourceResolver)
        m_sourceResolver->release(m_resolvedSource);
    m_resolvedSource = QUrl{};
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
    const auto &item = queue.at(queue.currentIndex());
    if (item.metadata.isRemote()) {
        m_backend.setSource({});
        m_resolving = true;
        setState(play ? PlayerState::Loading : PlayerState::Paused);
        emit trackChanged();
        if (m_reloadingExtensions.contains(item.metadata.providerId.section('/', 0, 0))) {
            m_resolving = false;
            m_waitingForReload = true;
            return;
        }
        if (!m_sourceResolver) {
            m_resolving = false;
            setState(PlayerState::Error);
            emit errorOccurred("Music source resolver unavailable");
            return;
        }
        QPointer<PlayerEngine> guard(this);
        auto *resolver = m_sourceResolver;
        resolver->resolve(item.metadata, [guard, generation, resolver](Result<QUrl> result) {
            if (!guard || generation != guard->m_sourceGeneration) {
                if (result)
                    resolver->release(result.value());
                return;
            }
            guard->m_resolving = false;
            if (!result) {
                guard->m_playIntent = false;
                guard->setState(PlayerState::Error);
                emit guard->errorOccurred(result.error().message);
                return;
            }
            guard->m_resolvedSource = result.value();
            guard->m_backend.setSource(result.value());
            if (guard->m_playIntent)
                guard->m_backend.play();
            else
                guard->setState(PlayerState::Paused);
        });
        return;
    }
    m_backend.setSource(QUrl::fromLocalFile(queue.at(queue.currentIndex()).path));
    emit trackChanged();
    if (play)
        m_backend.play();
}
void PlayerEngine::clearSource() {
    ++m_sourceGeneration;
    m_resolving = false;
    m_waitingForReload = false;
    m_playIntent = false;
    if (m_sourceResolver)
        m_sourceResolver->release(m_resolvedSource);
    m_resolvedSource = QUrl{};
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
    if (m_resolving || m_waitingForReload) {
        m_playIntent = true;
        setState(PlayerState::Loading);
        return {};
    }
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
    m_playIntent = false;
    if (m_resolving || m_waitingForReload) {
        setState(PlayerState::Paused);
        return {};
    }
    m_backend.pause();
    return {};
}
Result<void> PlayerEngine::stop() {
    const auto &current = m_queue.queue();
    if (m_resolving ||
        current.currentIndex() >= 0 && current.at(current.currentIndex()).metadata.isRemote()) {
        clearSource();
        return {};
    }
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
void PlayerEngine::sourceUnavailable(const QString &provider) {
    const auto &current = m_queue.queue();
    if (current.currentIndex() < 0 || current.at(current.currentIndex()).metadata.providerId != provider)
        return;
    if (m_waitingForReload || (!m_resolving && m_backend.source().isEmpty()))
        return;
    clearSource();
    setState(PlayerState::Error);
    emit errorOccurred("Music source unavailable: " + provider);
}
void PlayerEngine::sourcesReloading(const QStringList &extensionIds, bool active) {
    for (const auto &id : extensionIds) {
        if (active)
            m_reloadingExtensions.insert(id);
        else
            m_reloadingExtensions.remove(id);
    }
    const auto &queue = m_queue.queue();
    if (queue.currentIndex() < 0)
        return;
    const auto &song = queue.at(queue.currentIndex()).metadata;
    if (!song.isRemote() || !extensionIds.contains(song.providerId.section('/', 0, 0)))
        return;
    if (active && (m_resolving || !m_backend.source().isEmpty()))
        loadCurrent(playing());
    else if (!active && m_waitingForReload &&
             !m_reloadingExtensions.contains(song.providerId.section('/', 0, 0)))
        loadCurrent(m_playIntent);
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
    ++m_sourceGeneration;
    m_resolving = false;
    m_waitingForReload = false;
    m_reloadingExtensions.clear();
    m_playIntent = false;
    if (m_sourceResolver)
        m_sourceResolver->release(m_resolvedSource);
    m_resolvedSource = QUrl{};
    m_metadataTimer.stop();
    m_backend.stop();
    m_backend.setSource({});
}
} // namespace nekotune
