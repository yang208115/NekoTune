#include "infrastructure/music_directory.h"
#include "app_paths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

namespace nekotune {
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
} // namespace nekotune
