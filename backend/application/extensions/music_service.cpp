#include "application/extensions/music_service.h"
#include <QPointer>
#include <QUuid>
namespace nekotune {
MusicService::MusicService(ExtensionService &extensions, LibraryService &library, IFileInspector &imports,
                           CommandScheduler &commands, Reserve reserve)
    : m_extensions(extensions), m_library(library), m_imports(imports), m_commands(commands),
      m_reserve(std::move(reserve)) {
    connect(&extensions, &ExtensionService::changed, this, &MusicService::sourcesChanged);
}
bool MusicService::available(const QString &source) const {
    for (const auto &item : sources())
        if (item.toMap().value("id").toString() == source)
            return true;
    return false;
}
void MusicService::resolve(const SongMetadata &song, ISourceResolver::Completion done) {
    m_extensions.request("extensions.resolve_audio",
                         {{"source", song.providerId}, {"id", song.providerTrackId}},
                         [done](Result<QVariantMap> result) {
                             if (!result)
                                 done(result.error());
                             else
                                 done(QUrl(result.value().value("url").toString()));
                         });
}
void MusicService::release(const QUrl &url) {
    if (!url.isEmpty())
        m_extensions.request("extensions.release_audio", {{"url", url.toString()}}, [](auto) {});
}
void MusicService::search(const QString &source, const QVariantMap &params,
                          IExtensionBackend::Completion done) {
    m_extensions.request("extensions.provider_call",
                         {{"source", source}, {"kind", "music"}, {"operation", "search"}, {"params", params}},
                         std::move(done));
}
void MusicService::track(const QString &source, const QString &id, IExtensionBackend::Completion done) {
    m_extensions.request(
        "extensions.provider_call",
        {{"source", source}, {"kind", "music"}, {"operation", "track"}, {"params", QVariantMap{{"id", id}}}},
        std::move(done));
}
void MusicService::cancelDownload(const QString &task, IExtensionBackend::Completion done) {
    if (!m_downloads.contains(task)) {
        done(failure("Download task not found"));
        return;
    }
    m_cancelled.insert(task);
    m_extensions.request("extensions.cancel_download", {{"task", task}}, std::move(done));
}
QString MusicService::download(const QString &source, const SongMetadata &song) {
    const auto task = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QPointer<MusicService> guard(this);
    m_downloads.insert(task);
    auto completed = std::make_shared<bool>(false);
    auto report = [guard, task, completed](QString state, const QString &message = {}, int songId = 0,
                                           QVariantMap details = {}) {
        if (!guard || *completed)
            return;
        if (state != "running") {
            *completed = true;
            guard->m_downloads.remove(task);
            if (guard->m_cancelled.remove(task))
                state = "cancelled";
        }
        details.insert("event", "music.download");
        details.insert("task", task);
        details.insert("state", state);
        details.insert("message", message);
        details.insert("song_id", songId);
        emit guard->taskChanged(details);
    };
    report("running");
    // Only reservation and import use the mutation queue; extension callbacks may themselves call host APIs.
    m_commands.submit(
        [this, source, song, guard, report, task](CommandScheduler::Done next) {
            if (m_cancelled.contains(task)) {
                next();
                report("cancelled");
                return;
            }
            auto base = m_reserve("extension/" + song.resourceKey(), song.customTitle);
            next();
            if (!base) {
                report("failed", base.error().message);
                return;
            }
            m_extensions.request(
                "extensions.download_audio",
                {{"source", source}, {"id", song.providerTrackId}, {"base", base.value()}, {"task", task}},
                [guard, song, source, report, task](Result<QVariantMap> result) {
                    if (!guard)
                        return;
                    if (!result) {
                        report("failed", result.error().message);
                        return;
                    }
                    const auto path = result.value().value("path").toString();
                    guard->m_commands.submit(
                        [guard, song, source, path, report, task](CommandScheduler::Done done) {
                            if (!guard) {
                                done();
                                return;
                            }
                            guard->m_imports.inspect(path, [guard, song, source, report, done,
                                                            task](Result<ImportedFile> file) {
                                if (!guard) {
                                    done();
                                    return;
                                }
                                if (!file) {
                                    done();
                                    report("failed", file.error().message);
                                    return;
                                }
                                const bool existing = guard->m_library.containsAudioHash(file.value().hash);
                                auto saved =
                                    guard->m_library.importFile(file.value(), song.customTitle, song.artist);
                                done();
                                if (!saved) {
                                    report("failed", saved.error().message);
                                    return;
                                }
                                const int songId = saved.value().id;
                                const auto importedPath = file.value().path;
                                if (guard->m_cancelled.contains(task)) {
                                    report("cancelled", {}, songId, {{"path", importedPath}});
                                    return;
                                }
                                guard->m_extensions.request(
                                    "extensions.download_completed",
                                    {{"source", source},
                                     {"id", song.providerTrackId},
                                     {"path", importedPath},
                                     {"song_id", songId},
                                     {"existing", existing}},
                                    [guard, report, songId, importedPath](Result<QVariantMap> assets) {
                                        QVariantMap details =
                                            assets ? assets.value()
                                                   : QVariantMap{{"asset_warning", assets.error().message}};
                                        details.insert("path", importedPath);
                                        if (guard)
                                            emit guard->m_library.changed();
                                        report("finished", {}, songId, details);
                                    });
                            });
                        },
                        [report] { report("cancelled"); });
                });
        },
        [report] { report("cancelled"); });
    return task;
}
} // namespace nekotune
