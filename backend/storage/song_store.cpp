#include "storage/song_store.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

namespace nekotune {

SongStore::SongStore(const QString &databasePath, const QString &connectionName)
    : m_connectionName(connectionName.isEmpty()
                           ? QStringLiteral("nekotune-song-store-%1")
                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
                           : connectionName)
{
    m_ready = initialize(databasePath);
}

SongStore::~SongStore()
{
    const QString connectionName = m_connectionName;
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

bool SongStore::isReady() const
{
    return m_ready;
}

QString SongStore::errorString() const
{
    return m_error;
}

QString SongStore::databasePath() const
{
    return m_databasePath;
}

std::optional<SongMetadata> SongStore::getOrCreateSong(const QString &hash, const QString &path)
{
    if (!m_ready) {
        return std::nullopt;
    }

    if (auto existing = songByHash(hash)) {
        return existing;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO songs (hash, first_path) "
        "VALUES (:hash, :first_path)"));
    query.bindValue(QStringLiteral(":hash"), hash);
    query.bindValue(QStringLiteral(":first_path"), path);

    if (!query.exec()) {
        setError(query.lastError().text());
        return std::nullopt;
    }

    return songById(query.lastInsertId().toInt());
}

QVector<SongMetadata> SongStore::songs() const
{
    QVector<SongMetadata> items;
    if (!m_ready) {
        return items;
    }

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT id, hash, first_path, custom_title, artist, lyrics "
            "FROM songs ORDER BY id ASC"))) {
        return items;
    }

    while (query.next()) {
        items.append(SongMetadata {
            query.value(0).toInt(),
            query.value(1).toString(),
            query.value(2).toString(),
            query.value(3).toString(),
            query.value(4).toString(),
            query.value(5).toString(),
        });
    }

    return items;
}

std::optional<SongMetadata> SongStore::songById(int songId) const
{
    if (!m_ready || songId <= 0) {
        return std::nullopt;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, hash, first_path, custom_title, artist, lyrics "
        "FROM songs WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), songId);

    if (!query.exec()) {
        return std::nullopt;
    }

    return readSongFromQuery(query);
}

std::optional<SongMetadata> SongStore::updateMetadata(int songId,
                                                      const QString &customTitle,
                                                      const QString &artist,
                                                      const QString &lyrics)
{
    if (!m_ready || songId <= 0) {
        return std::nullopt;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE songs "
        "SET custom_title = :custom_title, artist = :artist, lyrics = :lyrics, "
        "updated_at = CURRENT_TIMESTAMP "
        "WHERE id = :id"));
    query.bindValue(QStringLiteral(":custom_title"), customTitle);
    query.bindValue(QStringLiteral(":artist"), artist);
    query.bindValue(QStringLiteral(":lyrics"), lyrics);
    query.bindValue(QStringLiteral(":id"), songId);

    if (!query.exec()) {
        setError(query.lastError().text());
        return std::nullopt;
    }

    return songById(songId);
}

bool SongStore::saveAsrTask(const QString &taskId, const QString &songHash, const QString &model, const QString &language)
{
    if (!m_ready)
        return false;
    if (taskId.isEmpty() || songHash.isEmpty() || model.isEmpty() || language.isEmpty()) {
        setError(QStringLiteral("ASR task backup fields must not be empty"));
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO asr_tasks(task_id, song_hash, model, language) VALUES(?, ?, ?, ?) "
                                 "ON CONFLICT(task_id) DO NOTHING"));
    query.addBindValue(taskId);
    query.addBindValue(songHash);
    query.addBindValue(model);
    query.addBindValue(language);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return true;
}

bool SongStore::saveTranscription(const QString &hash, const QByteArray &json)
{
    if (!m_ready || hash.isEmpty() || !QJsonDocument::fromJson(json).isObject())
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO song_transcriptions(song_hash, transcription_json) VALUES(?, ?) "
                                 "ON CONFLICT(song_hash) DO UPDATE SET transcription_json=excluded.transcription_json, updated_at=CURRENT_TIMESTAMP"));
    query.addBindValue(hash);
    query.addBindValue(QString::fromUtf8(json));
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return true;
}

QByteArray SongStore::transcription(const QString &hash) const
{
    if (!m_ready)
        return {};
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT transcription_json FROM song_transcriptions WHERE song_hash=?"));
    query.addBindValue(hash);
    return query.exec() && query.next() ? query.value(0).toString().toUtf8() : QByteArray{};
}

QString SongStore::defaultDatabasePath()
{
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString explicitPath = env.value(QStringLiteral("NEKOTUNE_DB_PATH"));
    if (!explicitPath.isEmpty()) {
        return explicitPath;
    }

    const QDir currentDir(QDir::currentPath());
    if (currentDir.exists(QStringLiteral("CMakeLists.txt"))
        && currentDir.exists(QStringLiteral("backend"))
        && currentDir.exists(QStringLiteral("build"))) {
        return currentDir.filePath(QStringLiteral("build/nekotune.sqlite3"));
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    if (appDir.exists(QStringLiteral("../CMakeCache.txt"))) {
        return QFileInfo(QDir(appDir.filePath(QStringLiteral("..")))
                             .filePath(QStringLiteral("nekotune.sqlite3")))
            .absoluteFilePath();
    }
    if (appDir.exists(QStringLiteral("CMakeCache.txt"))) {
        return appDir.filePath(QStringLiteral("nekotune.sqlite3"));
    }

    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDir.isEmpty()) {
        dataDir = QDir::currentPath();
    }

    return QDir(dataDir).filePath(QStringLiteral("nekotune.sqlite3"));
}

bool SongStore::initialize(const QString &databasePath)
{
    m_databasePath = databasePath;
    const QFileInfo databaseFile(databasePath);
    const QString parentPath = databaseFile.absolutePath();
    if (!parentPath.isEmpty() && !QDir().mkpath(parentPath)) {
        setError(QStringLiteral("Unable to create database directory: %1").arg(parentPath));
        return false;
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(databasePath);

    if (!m_db.open()) {
        setError(m_db.lastError().text());
        return false;
    }

    return migrate();
}

bool SongStore::migrate()
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS songs ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "hash TEXT NOT NULL UNIQUE,"
            "first_path TEXT NOT NULL,"
            "custom_title TEXT NOT NULL DEFAULT '',"
            "artist TEXT NOT NULL DEFAULT '',"
            "lyrics TEXT NOT NULL DEFAULT '',"
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ")"))) {
        setError(query.lastError().text());
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS queue_items ("
            "position INTEGER PRIMARY KEY,"
            "path TEXT NOT NULL,"
            "song_id INTEGER NOT NULL,"
            "current_index INTEGER NOT NULL DEFAULT -1"
            ")"))) {
        setError(query.lastError().text());
        return false;
    }

    if (!m_db.transaction()) {
        setError(m_db.lastError().text());
        return false;
    }
    const QStringList statements {
        QStringLiteral("CREATE TABLE IF NOT EXISTS asr_tasks (task_id TEXT PRIMARY KEY NOT NULL, "
                       "song_hash TEXT NOT NULL REFERENCES songs(hash) ON DELETE CASCADE, model TEXT NOT NULL, "
                       "language TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS song_transcriptions (song_hash TEXT PRIMARY KEY REFERENCES songs(hash) ON DELETE CASCADE, transcription_json TEXT NOT NULL, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS playlists (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS playlist_items (playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE, song_id INTEGER NOT NULL REFERENCES songs(id), path TEXT NOT NULL, position INTEGER NOT NULL, PRIMARY KEY (playlist_id, song_id))"),
    };
    for (const auto &statement : statements) {
        if (!query.exec(statement)) {
            setError(query.lastError().text());
            m_db.rollback();
            return false;
        }
    }
    if (!migrateFolders()) {
        m_db.rollback();
        return false;
    }
    if (!m_db.commit()) {
        setError(m_db.lastError().text());
        m_db.rollback();
        return false;
    }
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        setError(query.lastError().text());
        return false;
    }
    return true;
}

bool SongStore::migrateFolders()
{
    QSqlQuery query(m_db);
    auto exec = [&](const QString &sql) {
        if (query.exec(sql)) return true;
        setError(query.lastError().text());
        return false;
    };
    if (m_db.tables().contains(QStringLiteral("queue_folders"))) {
        if (!exec(QStringLiteral("SELECT data FROM queue_folders WHERE id = 1"))) return false;
        QJsonArray folders;
        if (query.next()) {
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(query.value(0).toByteArray(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
                setError(QStringLiteral("Unable to migrate invalid folder data"));
                return false;
            }
            folders = document.array();
        }
        query.finish();
        QHash<int, QJsonObject> byId;
        for (const auto &value : folders) {
            const auto folder = value.toObject();
            const int id = folder.value("id").toInt();
            if (id <= 0 || byId.contains(id) || folder.value("name").toString().trimmed().isEmpty()) {
                setError(QStringLiteral("Unable to migrate invalid folder data"));
                return false;
            }
            byId.insert(id, folder);
        }
        for (const auto &value : folders) {
            const auto folder = value.toObject();
            QStringList names;
            QSet<int> visited;
            int ancestor = folder.value("id").toInt();
            while (ancestor != 0) {
                if (visited.contains(ancestor) || !byId.contains(ancestor)) {
                    setError(QStringLiteral("Unable to migrate invalid folder hierarchy"));
                    return false;
                }
                visited.insert(ancestor);
                names.prepend(byId.value(ancestor).value("name").toString());
                ancestor = byId.value(ancestor).value("parent_id").toInt();
            }
            query.prepare(QStringLiteral("INSERT INTO playlists (name) VALUES (:name)"));
            query.bindValue(QStringLiteral(":name"), names.join(QStringLiteral(" / ")));
            if (!query.exec()) { setError(query.lastError().text()); return false; }
            const int playlistId = query.lastInsertId().toInt();
            query.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO playlist_items (playlist_id, song_id, path, position) "
                "SELECT :playlist_id, song_id, path, position FROM queue_items "
                "WHERE folder_id = :folder_id AND song_id IN (SELECT id FROM songs) ORDER BY position"));
            query.bindValue(QStringLiteral(":playlist_id"), playlistId);
            query.bindValue(QStringLiteral(":folder_id"), folder.value("id").toInt());
            if (!query.exec()) { setError(query.lastError().text()); return false; }
        }
        if (!exec(QStringLiteral("DROP TABLE queue_folders"))) return false;
    }
    if (!exec(QStringLiteral("PRAGMA table_info(queue_items)"))) return false;
    bool hasFolder = false;
    while (query.next()) hasFolder |= query.value(1).toString() == QStringLiteral("folder_id");
    query.finish();
    if (hasFolder && !exec(QStringLiteral("ALTER TABLE queue_items DROP COLUMN folder_id"))) return false;
    return true;
}

bool SongStore::saveQueue(const QueueSnapshot &snapshot)
{
    if (!m_ready) {
        return false;
    }

    if (!m_db.transaction()) {
        setError(m_db.lastError().text());
        return false;
    }

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM queue_items"))) {
        setError(query.lastError().text());
        m_db.rollback();
        return false;
    }

    query.prepare(QStringLiteral(
        "INSERT INTO queue_items (position, path, song_id, current_index) "
        "VALUES (:position, :path, :song_id, :current_index)"));
    for (int index = 0; index < snapshot.items.size(); ++index) {
        const auto &item = snapshot.items.at(index);
        query.bindValue(QStringLiteral(":position"), index);
        query.bindValue(QStringLiteral(":path"), item.path);
        query.bindValue(QStringLiteral(":song_id"), item.songId);
        query.bindValue(QStringLiteral(":current_index"), snapshot.currentIndex);
        if (!query.exec()) {
            setError(query.lastError().text());
            m_db.rollback();
            return false;
        }
    }

    if (!m_db.commit()) {
        setError(m_db.lastError().text());
        m_db.rollback();
        return false;
    }
    return true;
}

QueueSnapshot SongStore::loadQueue() const
{
    QueueSnapshot snapshot;
    if (!m_ready) {
        return snapshot;
    }

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT path, song_id, current_index FROM queue_items ORDER BY position ASC"))) {
        return snapshot;
    }

    while (query.next()) {
        snapshot.items.append({query.value(0).toString(), query.value(1).toInt()});
        snapshot.currentIndex = query.value(2).toInt();
    }
    if (snapshot.items.isEmpty()) {
        snapshot.currentIndex = -1;
    }
    return snapshot;
}

std::optional<SongMetadata> SongStore::songByHash(const QString &hash) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, hash, first_path, custom_title, artist, lyrics "
        "FROM songs WHERE hash = :hash"));
    query.bindValue(QStringLiteral(":hash"), hash);

    if (!query.exec()) {
        return std::nullopt;
    }

    return readSongFromQuery(query);
}

std::optional<SongMetadata> SongStore::readSongFromQuery(QSqlQuery &query) const
{
    if (!query.next()) {
        return std::nullopt;
    }

    return SongMetadata {
        query.value(0).toInt(),
        query.value(1).toString(),
        query.value(2).toString(),
        query.value(3).toString(),
        query.value(4).toString(),
        query.value(5).toString(),
    };
}

void SongStore::setError(const QString &message)
{
    m_error = message;
}

} // namespace nekotune
