#include "infrastructure/lyrics/sidecar_store.h"
#include "infrastructure/kugou/kugou_download_job.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkReply>
#include <QSaveFile>

namespace nekotune {
namespace {
bool atomicWrite(const QString &path, const QByteArray &bytes) {
    // Sidecars are owned files; never follow a link or fall back to truncating the destination in place.
    if (QFileInfo(path).isSymLink())
        return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool removeSidecar(const QString &path) {
    const QFileInfo info(path);
    return !info.exists() && !info.isSymLink() ? true : !info.isSymLink() && QFile::remove(path);
}
} // namespace
SidecarStore::SidecarStore(QNetworkAccessManager *manager)
    : m_manager(manager ? manager : new QNetworkAccessManager(this)) {}
void SidecarStore::shutdown() {
    ++m_generation;
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}
void SidecarStore::setCurrent(const QString &hash, quint64 revision, bool offline) {
    if (m_hash != hash || m_revision != revision || m_offline != offline)
        shutdown();
    m_hash = hash;
    m_revision = revision;
    m_offline = offline;
}
bool SidecarStore::current(const QString &hash, quint64 revision, quint64 generation) const {
    // Generation also distinguishes two saves within the same lyric revision, not just track changes.
    return hash == m_hash && revision == m_revision && generation == m_generation;
}
// Only the current song/revision is eligible for managed sidecar writes.
// Cancel any older cover save before starting this asset generation.
// Remove obsolete KRC when a newly selected result no longer has it,
// otherwise local-priority loading would keep choosing stale word timing.
// Retain existing artwork when the provider supplies no replacement URL.
// Lyric writes can succeed even if optional cover validation later fails.
void SidecarStore::save(const QString &hash, quint64 revision, const QString &base,
                        const LyricsDocument &document) {
    if (base.isEmpty() || hash != m_hash || revision != m_revision)
        return;
    shutdown();
    if (!QDir().mkpath(QFileInfo(base).absolutePath())) {
        emit failed(hash, "Cannot create song asset directory");
        return;
    }
    const auto lrc = document.syncedLyrics.isEmpty() ? document.plainLyrics : document.syncedLyrics;
    bool ok = document.krcLyrics.isEmpty() ? removeSidecar(base + ".krc")
                                           : atomicWrite(base + ".krc", document.krcLyrics.toUtf8());
    ok = (lrc.isEmpty() ? removeSidecar(base + ".lrc") : atomicWrite(base + ".lrc", lrc.toUtf8())) && ok;
    if (!ok)
        emit failed(hash, "Cannot save song lyrics");
    // Lyrics-only providers have no replacement artwork.
    if (!document.coverUrl.isEmpty())
        for (const auto &suffix : {".jpg", ".jpeg", ".png", ".webp"})
            if (!removeSidecar(base + QLatin1String(suffix)))
                emit failed(hash, "Cannot replace song artwork");
    emit saved(hash);
    const auto url = KugouDownloadJob::trustedCoverUrl(document.coverUrl);
    if (!m_offline && !url.isEmpty())
        fetchCover(hash, revision, m_generation, base, url);
}
// Remote artwork is optional and remains bounded in bytes and pixels.
// Each redirect is checked against the trusted HTTPS image endpoint.
// Hash, revision and save generation must all still match on completion.
// This stops an earlier cover from replacing a newer lyric selection.
// Only decoded JPEG/PNG/WebP content becomes a managed image sidecar.
// Signal failure without undoing an already saved lyric document.
void SidecarStore::fetchCover(const QString &hash, quint64 revision, quint64 generation, const QString &base,
                              const QUrl &url, int redirects) {
    if (!current(hash, revision, generation) || m_offline)
        return;
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_manager->get(request);
    m_reply = reply;
    auto bytes = std::make_shared<QByteArray>();
    auto oversized = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, bytes, oversized] {
        const auto chunk = reply->readAll();
        if (bytes->size() + chunk.size() > 5 * 1024 * 1024) {
            *oversized = true;
            reply->abort();
        } else
            bytes->append(chunk);
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, hash, revision, generation, base, redirects, bytes, oversized] {
                if (m_reply == reply)
                    m_reply = nullptr;
                reply->deleteLater();
                if (!current(hash, revision, generation) || m_offline)
                    return;
                const auto http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (http >= 300 && http < 400 && redirects < 3) {
                    const auto next = reply->url().resolved(
                        reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
                    const auto trusted = KugouDownloadJob::trustedCoverUrl(next.toString());
                    if (next.scheme() == "https" && next == trusted) {
                        fetchCover(hash, revision, generation, base, next, redirects + 1);
                        return;
                    }
                }
                if (http != 200 || reply->error() != QNetworkReply::NoError || *oversized) {
                    emit failed(hash, "Cannot download song artwork");
                    return;
                }
                bytes->append(reply->readAll());
                QBuffer buffer(bytes.get());
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                // Byte limits do not bound decoded pixels; inspect dimensions and decode before publishing.
                const auto size = reader.size();
                const auto format = reader.format();
                QString suffix;
                if (format == "jpeg" || format == "jpg")
                    suffix = ".jpg";
                else if (format == "png")
                    suffix = ".png";
                else if (format == "webp")
                    suffix = ".webp";
                if (bytes->size() > 5 * 1024 * 1024 || suffix.isEmpty() || !size.isValid() ||
                    size.width() > 4096 || size.height() > 4096 || reader.read().isNull() ||
                    !atomicWrite(base + suffix, *bytes)) {
                    emit failed(hash, "Cannot save valid song artwork");
                    return;
                }
                emit saved(hash);
            });
}
} // namespace nekotune
