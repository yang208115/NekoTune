#include "application/cover_service.h"
#include <QDir>
#include <QFileInfo>
#include <QUrl>

namespace nekotune {
CoverService::CoverService(std::unique_ptr<ILyricsStorage> storage) : m_storage(std::move(storage)) {}

QString CoverService::cachedCover(const QString &trackId) const {
    if (trackId.isEmpty())
        return {};
    if (!m_covers.contains(trackId)) {
        LyricsQuery query;
        query.trackId = trackId;
        const auto document = m_storage->readCache(query);
        m_covers.insert(trackId, document ? document->coverUrl : QString());
    }
    return m_covers.value(trackId);
}

QString CoverService::resolve(const QString &path, const QString &trackId) const {
    if (!path.isEmpty()) {
        const QFileInfo audio(path);
        const auto base = QDir(audio.absolutePath()).filePath(audio.completeBaseName());
        for (const auto &suffix : {".jpg", ".jpeg", ".png", ".webp"})
            if (QFileInfo(base + QLatin1String(suffix)).isFile())
                return QUrl::fromLocalFile(base + QLatin1String(suffix)).toString();
    }
    return m_offline ? QString() : cachedCover(trackId);
}

void CoverService::updateLyrics(const LyricsSnapshot &state) {
    bool updated = m_offline != state.offline;
    m_offline = state.offline;
    if (!state.trackId.isEmpty() && state.document) {
        const auto previous = m_covers.value(state.trackId);
        // The lyrics worker commits its cache before publishing the selected
        // document. Invalidate even an empty result so a later selection can supply
        // artwork.
        m_covers.remove(state.trackId);
        const auto cover =
            state.document->coverUrl.isEmpty() ? cachedCover(state.trackId) : state.document->coverUrl;
        m_covers.insert(state.trackId, cover);
        updated = updated || previous != cover;
    }
    if (updated)
        emit changed();
}
} // namespace nekotune
