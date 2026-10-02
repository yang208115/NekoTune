#include "application/lyrics_controller.h"
namespace nekotune {
LyricsController::LyricsController(PlayerEngine &player, LyricsService *service)
    : m_player(player), m_service(service), m_sources(service->sources()) {
    m_snapshot.offline = service->offline();
    qRegisterMetaType<LyricsSnapshot>();
    qRegisterMetaType<LyricsQuery>();
    qRegisterMetaType<LyricsDocument>();
    m_service->moveToThread(&m_thread);
    connect(m_service, &LyricsService::assetsReady, this,
            [this](const LyricsQuery &request, const LyricsDocument &document, quint64 revision) {
                if (revision == m_revision && request.trackId == query().trackId)
                    emit assetsReady(request.trackId, document, revision);
            });
    connect(&m_thread, &QThread::finished, m_service, &QObject::deleteLater);
    connect(m_service, &LyricsService::changed, this, [this](const LyricsSnapshot &state) {
        if (state.revision != m_revision || state.trackId != query().trackId)
            return;
        m_snapshot = state;
        emit changed(m_snapshot);
    });
    connect(&player, &PlayerEngine::lyricsNeeded, this, [this](bool ready) {
        m_ready = ready;
        load(ready);
    });
    m_thread.start();
    load(false);
}
LyricsController::~LyricsController() { shutdown(); }
void LyricsController::shutdown() {
    disconnect(&m_player, nullptr, this, nullptr);
    if (!m_thread.isRunning())
        return;
    QMetaObject::invokeMethod(m_service, [this] { m_service->shutdown(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}
LyricsQuery LyricsController::query() const {
    auto state = m_player.snapshot();
    if (!state.song)
        return {};
    return {state.metadata.title, state.metadata.artist, state.metadata.album, m_ready ? state.duration : 0,
            state.song->metadata.hash};
}
void LyricsController::load(bool ready, bool force) {
    auto request = query();
    auto current = m_player.snapshot();
    auto revision = ++m_revision;
    bool offline = m_snapshot.offline;
    m_snapshot = {};
    m_snapshot.trackId = request.trackId;
    m_snapshot.revision = revision;
    m_snapshot.offline = offline;
    m_snapshot.state = request.trackId.isEmpty() ? QStringLiteral("idle") : QStringLiteral("loading");
    emit changed(m_snapshot);
    if (!current.song) {
        QMetaObject::invokeMethod(m_service, [service = m_service, revision] { service->clear(revision); });
        return;
    }
    QMetaObject::invokeMethod(
        m_service, [service = m_service, request, item = *current.song, revision, ready, force] {
            service->load(request, item.path, item.metadata.lyrics, revision, ready, force);
        });
}
Result<void> LyricsController::refresh(const QString &trackId) {
    if (trackId.isEmpty() || trackId != query().trackId)
        return failure(QStringLiteral("Track is no longer current"));
    load(m_ready, true);
    return {};
}
Result<void> LyricsController::search(const QString &trackId, const MetadataPatch &fields,
                                      const QString &album, const QString &source) {
    auto request = query();
    if (trackId.isEmpty() || trackId != request.trackId)
        return failure(QStringLiteral("Track is no longer current"));
    bool valid = false;
    for (const auto &item : m_sources)
        if (item.id == source && item.supportsSearch)
            valid = true;
    if (!valid)
        return failure(QStringLiteral("Invalid lyrics source"));
    request.title = fields.title.value_or(request.title).trimmed();
    request.artist = fields.artist.value_or(request.artist).trimmed();
    if (!album.isNull())
        request.album = album.trimmed();
    if (request.title.isEmpty())
        return failure(QStringLiteral("Search title is required"));
    m_player.cancelPendingMetadataRefresh();
    auto revision = ++m_revision;
    m_snapshot.revision = revision;
    m_snapshot.state = QStringLiteral("searching");
    emit changed(m_snapshot);
    QMetaObject::invokeMethod(m_service, [service = m_service, request, revision, source] {
        service->search(request, revision, source);
    });
    return {};
}
Result<void> LyricsController::select(const QString &trackId, quint64 revision, int index) {
    if (trackId.isEmpty() || trackId != query().trackId || revision != m_revision)
        return failure(QStringLiteral("Lyrics results are no longer current"));
    if (index < 0 || index >= m_snapshot.candidates.size())
        return failure(QStringLiteral("Invalid lyrics candidate index"));
    QMetaObject::invokeMethod(m_service,
                              [service = m_service, index, revision] { service->select(index, revision); });
    return {};
}
void LyricsController::setOffline(bool offline) {
    m_snapshot.offline = offline;
    QMetaObject::invokeMethod(m_service, [service = m_service, offline] { service->setOffline(offline); });
    if (!offline)
        load(m_ready);
}
} // namespace nekotune
