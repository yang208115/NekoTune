#include "infrastructure/import_executor.h"
#include <QCryptographicHash>
#include <QDateTime>
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
            info.refresh();
            if (info.size() != size || info.lastModified() != modified)
                return failure(QStringLiteral("File changed during import"), ErrorCode::Io);
            return ImportedFile{info.absoluteFilePath(), QString::fromLatin1(hash.result().toHex())};
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
