#include "application/lyrics/cover_service.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>

namespace nekotune {
CoverService::CoverService(std::unique_ptr<ILyricsStorage> storage) : m_storage(std::move(storage)) {}

// Cache the absence of artwork too, avoiding repeated disk reads.
// The key is audio identity so changing display metadata is harmless.
// Selecting new lyrics or saving sidecars invalidates this memoization.
// The resolver never chooses a different lyric merely to obtain a cover.
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
    auto localCover = [&](const QString &base, bool managed) -> QString {
        for (const auto &suffix : {".jpg", ".jpeg", ".png", ".webp"})
            if (const QFileInfo cover(base + QLatin1String(suffix)); cover.isFile()) {
                auto url = QUrl::fromLocalFile(cover.absoluteFilePath());
                if (managed) {
                    // Replacements at the same path must invalidate QML's image cache.
                    QUrlQuery version;
                    version.addQueryItem("v", QString("%1-%2-%3")
                                                  .arg(cover.lastModified().toMSecsSinceEpoch())
                                                  .arg(cover.size())
                                                  .arg(m_localVersions.value(trackId)));
                    url.setQuery(version);
                }
                return url.toString();
            }
        return {};
    };
    const auto managedBase = m_managedBase ? m_managedBase(trackId) : QString();
    if (!managedBase.isEmpty())
        if (const auto cover = localCover(managedBase, true); !cover.isEmpty())
            return cover;
    if (!path.isEmpty()) {
        const QFileInfo audio(path);
        const auto base = audio.dir().filePath(audio.completeBaseName());
        bool numbered = false;
        audio.completeBaseName().toLongLong(&numbered);
        if (const auto cover = localCover(base, numbered && audio.completeBaseName() == audio.dir().dirName());
            !cover.isEmpty())
            return cover;
        if (audio.isSymLink() && audio.exists()) {
            const QFileInfo original(audio.canonicalFilePath());
            if (const auto cover = localCover(original.dir().filePath(original.completeBaseName()), false);
                !cover.isEmpty())
                return cover;
        }
    }
    return m_offline ? QString() : cachedCover(trackId);
}
void CoverService::assetsUpdated(const QString &trackId) {
    m_covers.remove(trackId);
    ++m_localVersions[trackId];
    emit changed();
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
