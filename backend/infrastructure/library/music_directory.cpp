#include "infrastructure/library/music_directory.h"
#include "app_paths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QDebug>

namespace nekotune {
namespace {
class ManagedFileRemoval final : public IManagedFileRemoval {
  public:
    QVector<QPair<QString, QString>> files;
    QStringList folders;
    ~ManagedFileRemoval() override {
        if (m_committed)
            return;
        for (auto it = files.crbegin(); it != files.crend(); ++it)
            if (!QDir().rename(it->second, it->first))
                qCritical() << "Cannot restore staged music file:" << it->second;
    }
    QStringList commit() override {
        m_committed = true;
        QStringList errors;
        for (const auto &[original, staged] : files)
            if (!QFile::remove(staged))
                errors.append(staged);
        for (const auto &folder : folders)
            QDir().rmdir(folder); // Only remove empty folders; preserve unrelated files.
        return errors;
    }
  private:
    bool m_committed = false;
};
}
MusicDirectory::MusicDirectory(DatabaseSession &database, const QString &directory)
    : m_database(database), m_directory(directory.isEmpty() ? AppPaths::musicDirectory()
                                                            : QFileInfo(directory).absoluteFilePath()) {}
QString MusicDirectory::base(qint64 id) const {
    const auto number = QString::number(id).rightJustified(6, '0');
    return QDir(m_directory).filePath(number + '/' + number);
}
Result<qint64> MusicDirectory::allocate(const QString &sourceName) {
    if (!QDir().mkpath(m_directory))
        return failure("Cannot create music directory", ErrorCode::Io);
    // Account for existing folders even after a restored or replaced database.
    qint64 maximum = 0;
    for (const auto &name : QDir(m_directory).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const auto number = name.toLongLong(&ok);
        if (ok && number > maximum)
            maximum = number;
    }
    QSqlQuery query(m_database.database());
    query.prepare("INSERT INTO managed_resources(id, source_name) VALUES "
                  "((SELECT max(coalesce((SELECT seq FROM sqlite_sequence WHERE "
                  "name='managed_resources'),0),"
                  " :maximum)+1), :name)");
    query.bindValue(":maximum", maximum);
    query.bindValue(":name", sourceName.isNull() ? QStringLiteral("") : sourceName);
    if (!query.exec())
        return failure(query.lastError().text(), ErrorCode::Storage);
    const auto id = query.lastInsertId().toLongLong();
    const auto folder = QFileInfo(base(id)).absolutePath();
    if (!QDir().mkpath(folder))
        return failure("Cannot create numbered music directory", ErrorCode::Io);
    return id;
}
Result<QString> MusicDirectory::reserveDownload(const QString &providerHash, const QString &title) {
    QSqlQuery query(m_database.database());
    query.prepare("SELECT resource_id FROM resource_sources WHERE provider_hash=:hash");
    query.bindValue(":hash", providerHash.toLower());
    if (!query.exec())
        return failure(query.lastError().text(), ErrorCode::Storage);
    if (query.next())
        return base(query.value(0).toLongLong());
    auto id = allocate(title);
    if (!id)
        return id.error();
    query.prepare("INSERT INTO resource_sources(provider_hash,resource_id) "
                  "VALUES(:hash,:id)");
    query.bindValue(":hash", providerHash.toLower());
    query.bindValue(":id", id.value());
    if (!query.exec())
        return failure(query.lastError().text(), ErrorCode::Storage);
    return base(id.value());
}
Result<ImportedFile> MusicDirectory::manage(const ImportedFile &file) {
    if (!m_database.isReady())
        return failure(m_database.errorString(), ErrorCode::Storage);
    QSqlQuery query(m_database.database());
    query.prepare("SELECT id,path,source_name FROM managed_resources WHERE hash=:hash");
    query.bindValue(":hash", file.hash);
    if (!query.exec())
        return failure(query.lastError().text(), ErrorCode::Storage);
    qint64 id = 0;
    QString path, name;
    if (query.next()) {
        id = query.value(0).toLongLong();
        path = query.value(1).toString();
        name = query.value(2).toString();
        if (QFileInfo(path).isFile()) {
            const QFileInfo incoming(file.path);
            const auto parent = incoming.dir().dirName();
            bool numbered = false;
            const auto incomingId = parent.toLongLong(&numbered);
            if (numbered && incoming.completeBaseName() == parent && incomingId != id &&
                incoming.dir().absolutePath() == QDir(m_directory).filePath(parent)) {
                query.prepare("UPDATE resource_sources SET resource_id=:id WHERE "
                              "resource_id=:old");
                query.bindValue(":id", id);
                query.bindValue(":old", incomingId);
                if (!query.exec())
                    return failure(query.lastError().text(), ErrorCode::Storage);
            }
            return ImportedFile{path, file.hash, name, file.durationMs};
        }
    }
    const QFileInfo input(file.path);
    if (!input.isFile())
        return failure("Song file is unavailable", ErrorCode::Io);
    if (name.isEmpty())
        name = file.sourceName.isEmpty() ? input.completeBaseName() : file.sourceName;
    const auto parent = input.dir().dirName();
    const bool numbered = !QFileInfo(input.absolutePath()).isSymLink() &&
                          QRegularExpression("^[0-9]{6,}$").match(parent).hasMatch() &&
                          input.completeBaseName() == parent &&
                          input.dir().absolutePath() == QDir(m_directory).filePath(parent);
    if (!id && numbered) {
        query.prepare("SELECT id,hash,source_name FROM managed_resources WHERE id=:id");
        query.bindValue(":id", parent.toLongLong());
        if (!query.exec())
            return failure(query.lastError().text(), ErrorCode::Storage);
        if (query.next()) {
            if (query.value(1).isNull() || query.value(1).toString() == file.hash) {
                id = query.value(0).toLongLong();
                if (!query.value(2).toString().isEmpty())
                    name = query.value(2).toString();
            }
        } else {
            query.prepare("INSERT INTO managed_resources(id,source_name) VALUES(:id,:name)");
            query.bindValue(":id", parent.toLongLong());
            query.bindValue(":name", name);
            if (!query.exec())
                return failure(query.lastError().text(), ErrorCode::Storage);
            id = parent.toLongLong();
        }
    }
    if (!id) {
        auto allocated = allocate(name);
        if (!allocated)
            return allocated.error();
        id = allocated.value();
    }
    path = base(id) + '.' + input.suffix().toLower();
    const bool alreadyManaged = input.absoluteFilePath() == path;
    bool linked = false;
    if (!alreadyManaged) {
        const QFileInfo target(path);
        if (target.isSymLink() && !target.exists())
            QFile::remove(path);
        if (QFileInfo::exists(path) || !QFile::link(input.canonicalFilePath(), path))
            return failure("Cannot create managed song symlink", ErrorCode::Io);
        linked = true;
    }
    query.prepare("UPDATE managed_resources SET "
                  "hash=:hash,path=:path,original_path=:original,"
                  "source_name=:name WHERE id=:id");
    query.bindValue(":hash", file.hash);
    query.bindValue(":path", path);
    query.bindValue(":original", input.isSymLink() ? input.symLinkTarget() : input.absoluteFilePath());
    query.bindValue(":name", name);
    query.bindValue(":id", id);
    if (!query.exec()) {
        if (linked)
            QFile::remove(path);
        return failure(query.lastError().text(), ErrorCode::Storage);
    }
    if (numbered && parent.toLongLong() != id) {
        query.prepare("UPDATE resource_sources SET resource_id=:id WHERE resource_id=:old");
        query.bindValue(":id", id);
        query.bindValue(":old", parent.toLongLong());
        if (!query.exec())
            return failure(query.lastError().text(), ErrorCode::Storage);
    }
    return ImportedFile{path, file.hash, name, file.durationMs};
}
Result<bool> MusicDirectory::ignored(const QString &hash) const {
    QSqlQuery query(m_database.database());
    query.prepare("SELECT 1 FROM scan_ignored WHERE hash=:hash");
    query.bindValue(":hash", hash);
    if (!query.exec())
        return failure(query.lastError().text(), ErrorCode::Storage);
    return query.next();
}
bool MusicDirectory::containsSong(const QString &hash) const {
    QSqlQuery query(m_database.database());
    query.prepare("SELECT 1 FROM songs WHERE hash=:hash");
    query.bindValue(":hash", hash);
    return query.exec() && query.next();
}
QString MusicDirectory::baseFor(const QString &hash) const {
    QSqlQuery query(m_database.database());
    query.prepare("SELECT path FROM managed_resources WHERE hash=:hash");
    query.bindValue(":hash", hash);
    if (!query.exec() || !query.next())
        return {};
    const QFileInfo path(query.value(0).toString());
    return path.dir().filePath(path.completeBaseName());
}
Result<std::unique_ptr<IManagedFileRemoval>> MusicDirectory::stageRemoval(const QStringList &hashes) {
    auto removal = std::make_unique<ManagedFileRemoval>();
    const auto root = QFileInfo(m_directory).canonicalFilePath();
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const auto &hash : hashes) {
        QSqlQuery query(m_database.database());
        query.prepare("SELECT id,path FROM managed_resources WHERE hash=:hash");
        query.bindValue(":hash", hash);
        if (!query.exec())
            return failure(query.lastError().text(), ErrorCode::Storage);
        if (!query.next())
            continue; // Legacy and external paths are never cleanup targets.
        const auto id = query.value(0).toLongLong();
        const auto expected = base(id);
        const auto folder = QFileInfo(expected).absolutePath();
        const QFileInfo folderInfo(folder);
        const QFileInfo audio(query.value(1).toString());
        if (root.isEmpty() || id <= 0 || folderInfo.isSymLink() ||
            audio.absoluteFilePath() != expected + '.' + audio.suffix() ||
            (folderInfo.exists() && folderInfo.canonicalFilePath() !=
                                       QDir(root).filePath(folderInfo.fileName())))
            return failure("Refusing to clean an invalid managed song directory", ErrorCode::Io);
        QStringList paths{audio.absoluteFilePath()};
        for (const auto &suffix : {".krc", ".lrc", ".jpg", ".jpeg", ".png", ".webp"})
            paths.append(expected + QLatin1String(suffix));
        for (const auto &path : paths) {
            const QFileInfo info(path);
            if (!info.exists() && !info.isSymLink())
                continue;
            if (!info.isFile() && !info.isSymLink())
                return failure("Managed song asset is not a file: " + path, ErrorCode::Io);
            const auto staged = QDir(folder).filePath('.' + info.fileName() + ".removing-" + token);
            if (!QDir().rename(path, staged))
                return failure("Cannot clean managed song file: " + path, ErrorCode::Io);
            removal->files.append({path, staged});
        }
        removal->folders.append(folder);
        query.prepare("DELETE FROM resource_sources WHERE resource_id=:id");
        query.bindValue(":id", id);
        if (!query.exec())
            return failure(query.lastError().text(), ErrorCode::Storage);
        query.prepare("DELETE FROM managed_resources WHERE id=:id");
        query.bindValue(":id", id);
        if (!query.exec())
            return failure(query.lastError().text(), ErrorCode::Storage);
    }
    return std::unique_ptr<IManagedFileRemoval>(std::move(removal));
}
} // namespace nekotune
