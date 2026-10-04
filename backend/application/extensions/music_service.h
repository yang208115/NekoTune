#pragma once
#include "application/command_scheduler.h"
#include "application/extensions/extension_service.h"
#include "application/library/library_service.h"
#include "domain/library/file_inspector.h"
#include "domain/playback/source_resolver.h"
#include <QSet>

namespace nekotune {
class MusicService final : public QObject, public ISourceResolver {
    Q_OBJECT
  public:
    using Reserve = std::function<Result<QString>(const QString &, const QString &)>;
    MusicService(ExtensionService &extensions, LibraryService &library, IFileInspector &imports,
                 CommandScheduler &commands, Reserve reserve);
    void resolve(const SongMetadata &, ISourceResolver::Completion) override;
    void release(const QUrl &) override;
    void search(const QString &source, const QVariantMap &params, IExtensionBackend::Completion done);
    void track(const QString &source, const QString &id, IExtensionBackend::Completion done);
    QString download(const QString &source, const SongMetadata &song);
    void cancelDownload(const QString &task, IExtensionBackend::Completion done);
    bool downloadActive() const { return !m_downloads.isEmpty(); }
    bool available(const QString &source) const;
    QVariantList sources() const { return m_extensions.sources("music"); }
  signals:
    void sourcesChanged();
    void taskChanged(const QVariantMap &event);

  private:
    ExtensionService &m_extensions;
    LibraryService &m_library;
    IFileInspector &m_imports;
    CommandScheduler &m_commands;
    Reserve m_reserve;
    QSet<QString> m_downloads, m_cancelled;
};
} // namespace nekotune
