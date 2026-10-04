#include "infrastructure/library/music_directory.h"
#include "infrastructure/library/audio_reference.h"
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
// Renames keep deleted assets recoverable until the database commits.
// Each pair records original and temporary path for reverse restoration.
// The destructor attempts recovery when the handle was never committed.
// Final commit removes entries and only attempts empty-directory cleanup.
// Unrelated files in a numbered directory therefore remain intact.
// Restore failures are logged because the destructor cannot return errors.
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
MusicDirectory::MusicDirectory(DatabaseSession &database, const QString &directory, LinkCreator createLink)
    : m_database(database), m_directory(directory.isEmpty() ? AppPaths::musicDirectory()
                                                            : QFileInfo(directory).absoluteFilePath()),
      m_createLink(createLink ? std::move(createLink) : createAudioSymlink) {}
QString MusicDirectory::base(qint64 id) const {
    const auto number = QString::number(id).rightJustified(6, '0');
    return QDir(m_directory).filePath(number + '/' + number);
}
// Numbered resources may outlive a replaced or restored database.
// Allocate above both SQLite's sequence and existing numbered folders.
// This avoids overwriting audio/sidecars whose registration was lost.
// The number identifies a resource, not a song's human-readable title.
// sourceName preserves that title hint separately for later display.
// Filesystem creation errors remain distinct from SQL allocation errors.
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
// Provider download identity is available before local bytes exist.
// Reserve a numbered basename using that provider hash now.
// After import, the audio's SHA-256 can merge identical downloads.
// Repeated provider requests reuse their existing reservation.
// No filename derived from remote title becomes the managed basename.
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
// Reuse a valid registered path for the same local audio hash first.
// A missing path can be repaired using the incoming inspected source.
// Reserved download folders can be adopted without moving their audio.
// An ordinary external file is represented by an absolute symlink.
// The mapping stores the source name before numbered naming hides it.
// SQL failure after linking removes only the link made by this operation.
// Resources and library records remain separate registrations by design.
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
        const auto referencePath = base(id) + ".audio.json";
        if (QFileInfo::exists(referencePath) || QFileInfo(referencePath).isSymLink()) {
            const auto reference = readAudioReference(referencePath);
            if (!reference || reference.value().hash != file.hash || reference.value().path != path)
                return failure("Managed audio reference does not match registration", ErrorCode::Io);
        }
        if (QFileInfo(path).isFile()) {
            // A provider may download identical audio into another reservation; reuse the audio identity
            // and retarget that provider's reservation without replacing the existing managed file.
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
    const QFileInfo origin(file.managedReferencePath.isEmpty() ? file.path : file.managedReferencePath);
    const auto parent = origin.dir().dirName();
    // A numeric-looking filename alone does not prove that a file is app-owned.
    // Adoption also requires the expected basename, exact root location and an unlinked directory.
    // Otherwise import creates a new managed audio link using the normal ownership boundary.
    const bool numbered = !QFileInfo(origin.absolutePath()).isSymLink() &&
                          QRegularExpression("^[0-9]{6,}$").match(parent).hasMatch() &&
                          (file.managedReferencePath.isEmpty() ? input.completeBaseName() == parent
                                                             : origin.fileName() == parent + ".audio.json") &&
                          origin.dir().absolutePath() == QDir(m_directory).filePath(parent);
    if (!file.managedReferencePath.isEmpty()) {
        const auto reference = readAudioReference(file.managedReferencePath);
        if (!numbered || !reference || reference.value().hash != file.hash ||
            reference.value().path != input.absoluteFilePath())
            return failure("Invalid discovered audio reference", ErrorCode::Io);
    }
    if (!id && numbered) {
        query.prepare("SELECT id,hash,source_name FROM managed_resources WHERE id=:id");
        query.bindValue(":id", parent.toLongLong());
        if (!query.exec())
            return failure(query.lastError().text(), ErrorCode::Storage);
        if (query.next()) {
            // An unfilled download reservation may acquire its first audio identity here.
            // An already filled reservation can be reused only for identical inspected bytes.
            // Different content must not inherit another song's numbered resource and sidecars.
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
    const QFileInfo folder(QFileInfo(base(id)).absolutePath());
    const auto root = QFileInfo(m_directory).canonicalFilePath();
    if (root.isEmpty() || folder.isSymLink() ||
        folder.canonicalFilePath() != QDir(root).filePath(folder.fileName()))
        return failure("Invalid managed song directory", ErrorCode::Io);
    path = base(id) + '.' + input.suffix().toLower();
    const bool alreadyManaged = input.absoluteFilePath() == path;
    bool linked = false;
    const auto referencePath = base(id) + ".audio.json";
    std::optional<AudioReference> previousReference;
    if (QFileInfo::exists(referencePath) || QFileInfo(referencePath).isSymLink()) {
        const auto reference = readAudioReference(referencePath);
        if (!reference || reference.value().hash != file.hash)
            return failure("Refusing to replace an unrelated audio reference", ErrorCode::Io);
        previousReference = reference.value();
    }
    bool referenceChanged = false;
    if (alreadyManaged && previousReference) {
        // A recovered/downloaded owned file replaces an unavailable external reference.
        // Do not leave a stale reference that would make later cleanup reject the registration.
        if (!QFile::remove(referencePath))
            return failure("Cannot replace managed audio reference", ErrorCode::Io);
        referenceChanged = true;
    } else if (!alreadyManaged) {
        // Link the canonical source so importing an existing symlink does not create a fragile chain.
        const QFileInfo target(path);
        // A dangling managed link is repairable because its target no longer exists.
        // Remove only that broken entry before linking the newly inspected source.
        // An existing usable file still blocks creation rather than being overwritten.
        if (target.isSymLink() && !target.exists() && !QFile::remove(path))
            return failure("Cannot replace broken managed song symlink", ErrorCode::Io);
        if (QFileInfo::exists(path))
            return failure("Cannot create managed song symlink", ErrorCode::Io);
        if (!previousReference && file.managedReferencePath.isEmpty() &&
            m_createLink(input.canonicalFilePath(), path)) {
            linked = true;
        } else {
            // Keep a real playback path even when the platform cannot create a native link.
            path = input.canonicalFilePath();
            if (!writeAudioReference(referencePath, {path, file.hash, name}))
                return failure("Cannot save managed audio reference", ErrorCode::Io);
            referenceChanged = true;
        }
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
        if (referenceChanged) {
            const bool restored = previousReference ? writeAudioReference(referencePath, *previousReference)
                                                    : QFile::remove(referencePath);
            if (!restored)
                qCritical() << "Cannot restore managed audio reference:" << referencePath;
        }
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
    query.prepare("SELECT id FROM managed_resources WHERE hash=:hash");
    query.bindValue(":hash", hash);
    if (!query.exec() || !query.next())
        return {};
    return base(query.value(0).toLongLong());
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
        const auto referencePath = expected + ".audio.json";
        const bool referenced = QFileInfo::exists(referencePath) || QFileInfo(referencePath).isSymLink();
        if (referenced) {
            const auto reference = readAudioReference(referencePath);
            if (!reference || reference.value().hash != hash ||
                reference.value().path != audio.absoluteFilePath())
                return failure("Refusing to clean an invalid audio reference", ErrorCode::Io);
        }
        // Stored paths alone are not authority to delete: verify the numbered layout and root,
        // rejecting directory symlinks that could redirect cleanup outside the managed tree.
        if (root.isEmpty() || id <= 0 || folderInfo.isSymLink() ||
            (!referenced && audio.absoluteFilePath() != expected + '.' + audio.suffix()) ||
            (folderInfo.exists() && folderInfo.canonicalFilePath() !=
                                       QDir(root).filePath(folderInfo.fileName())))
            return failure("Refusing to clean an invalid managed song directory", ErrorCode::Io);
        QStringList paths{referenced ? referencePath : audio.absoluteFilePath()};
        for (const auto &suffix : {".krc", ".lrc", ".jpg", ".jpeg", ".png", ".webp"})
            paths.append(expected + QLatin1String(suffix));
        for (const auto &path : paths) {
            const QFileInfo info(path);
            if (!info.exists() && !info.isSymLink())
                continue;
            if (!info.isFile() && !info.isSymLink())
                return failure("Managed song asset is not a file: " + path, ErrorCode::Io);
            // Rename the entry itself, including dangling audio links; never remove its external target.
            // Staging renames within the same numbered folder, keeping rollback on the same filesystem.
            // The per-operation token prevents another staged deletion from sharing temporary names.
            // The removal handle records an entry only after its rename succeeded.
            const auto staged = QDir(folder).filePath('.' + info.fileName() + ".removing-" + token);
            if (!QDir().rename(path, staged))
                return failure("Cannot clean managed song file: " + path, ErrorCode::Io);
            removal->files.append({path, staged});
        }
        removal->folders.append(folder);
        // Provider reservations must no longer point to a resource whose assets are being removed.
        // These SQL changes participate in the caller's surrounding deletion transaction.
        // Rollback restores registration while the handle restores names in reverse staging order.
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
