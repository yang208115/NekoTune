#include "infrastructure/library/import_executor.h"
#include "infrastructure/library/audio_duration.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
namespace nekotune {
ImportExecutor::ImportExecutor()
    : m_worker(new QObject), m_cancelled(std::make_shared<std::atomic_bool>(false)) {
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}
ImportExecutor::~ImportExecutor() { shutdown(); }
void ImportExecutor::shutdown() {
    if (m_cancelled->exchange(true))
        return;
    m_thread.quit();
    m_thread.wait();
    auto pending = std::move(m_pending);
    m_pending.clear();
    for (const auto &completion : pending)
        completion(failure(QStringLiteral("Import cancelled"), ErrorCode::Cancelled));
}
void ImportExecutor::complete(quint64 id, Result<ImportedFile> result) {
    auto completion = m_pending.take(id);
    if (completion)
        completion(std::move(result));
}
void ImportExecutor::inspect(const QString &path, Completion completion) {
    inspectUnmanaged(path, [this, completion = std::move(completion)](Result<ImportedFile> file) {
        completion(file && m_mapper ? m_mapper(file.value()) : std::move(file));
    });
}
void ImportExecutor::discover(const QString &directory, std::function<void(QStringList)> completion) {
    QPointer<ImportExecutor> guard(this);
    auto cancelled = m_cancelled;
    QMetaObject::invokeMethod(m_worker, [guard, cancelled, directory, completion] {
        QStringList paths, folders{directory};
        const QStringList suffixes{"mp3", "m4a", "aac", "wav", "flac", "ogg"};
        while (!folders.isEmpty() && !cancelled->load()) {
            const auto folder = folders.takeLast();
            for (const auto &entry :
                 QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (entry.isDir()) {
                    if (!entry.isSymLink() && entry.fileName() != "config")
                        folders.append(entry.absoluteFilePath());
                } else if (suffixes.contains(entry.suffix().toLower()))
                    paths.append(entry.absoluteFilePath());
            }
        }
        if (!guard || cancelled->load())
            return;
        QMetaObject::invokeMethod(guard, [guard, completion, paths] {
            if (guard)
                completion(paths);
        });
    });
}
void ImportExecutor::inspectUnmanaged(const QString &path, Completion completion) {
    if (m_cancelled->load()) {
        completion(failure(QStringLiteral("Import cancelled"), ErrorCode::Cancelled));
        return;
    }
    QPointer<ImportExecutor> guard(this);
    auto cancelled = m_cancelled;
    const auto id = m_nextId++;
    m_pending.insert(id, std::move(completion));
    QMetaObject::invokeMethod(m_worker, [guard, cancelled, path, id]() {
        auto inspect = [&]() -> Result<ImportedFile> {
            QFileInfo info(path);
            if (!info.isFile())
                return failure(QStringLiteral("File does not exist: %1").arg(path), ErrorCode::Io);
            auto size = info.size();
            auto modified = info.lastModified();
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly))
                return failure(QStringLiteral("Unable to read file: %1").arg(path), ErrorCode::Io);
            QCryptographicHash hash(QCryptographicHash::Sha256);
            while (!file.atEnd()) {
                if (cancelled->load())
                    return failure(QStringLiteral("Import cancelled"), ErrorCode::Cancelled);
                auto chunk = file.read(1024 * 1024);
                if (file.error() != QFileDevice::NoError)
                    return failure(file.errorString(), ErrorCode::Io);
                hash.addData(chunk);
            }
            const auto duration = readAudioDuration(info.absoluteFilePath(), *cancelled);
            if (cancelled->load())
                return failure(QStringLiteral("Import cancelled"), ErrorCode::Cancelled);
            info.refresh();
            if (info.size() != size || info.lastModified() != modified)
                return failure(QStringLiteral("File changed during import"), ErrorCode::Io);
            return ImportedFile{info.absoluteFilePath(), QString::fromLatin1(hash.result().toHex()),
                                info.isSymLink() ? QFileInfo(info.symLinkTarget()).completeBaseName()
                                                 : info.completeBaseName(),
                                duration};
        };
        auto result = inspect();
        if (!guard || cancelled->load())
            return;
        QMetaObject::invokeMethod(guard, [guard, id, result = std::move(result)]() mutable {
            if (guard)
                guard->complete(id, std::move(result));
        });
    });
}
} // namespace nekotune
