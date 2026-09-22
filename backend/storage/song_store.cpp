#include "storage/song_store.h"

#include <QCoreApplication>
#include <QJsonDocument>
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

    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS queue_folders (id INTEGER PRIMARY KEY CHECK (id = 1), data TEXT NOT NULL)"))) {
        setError(query.lastError().text());
        return false;
    }
    bool hasFolder = false;
    query.exec(QStringLiteral("PRAGMA table_info(queue_items)"));
    while (query.next()) hasFolder |= query.value(1).toString() == QStringLiteral("folder_id");
    if (!hasFolder && !query.exec(QStringLiteral("ALTER TABLE queue_items ADD COLUMN folder_id INTEGER NOT NULL DEFAULT 0"))) {
        setError(query.lastError().text());
        return false;
    }
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
        "INSERT INTO queue_items (position, path, song_id, current_index, folder_id) "
        "VALUES (:position, :path, :song_id, :current_index, :folder_id)"));
    for (int index = 0; index < snapshot.items.size(); ++index) {
        const auto &item = snapshot.items.at(index);
        query.bindValue(QStringLiteral(":folder_id"), item.folderId);
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

    query.prepare(QStringLiteral("INSERT OR REPLACE INTO queue_folders (id, data) VALUES (1, :data)"));
    query.bindValue(QStringLiteral(":data"), QString::fromUtf8(QJsonDocument(snapshot.folders).toJson(QJsonDocument::Compact)));
    if (!query.exec()) {
        setError(query.lastError().text());
        m_db.rollback();
        return false;
    }
    if (!m_db.commit()) {
        setError(m_db.lastError().text());
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
            "SELECT path, song_id, current_index, folder_id FROM queue_items ORDER BY position ASC"))) {
        return snapshot;
    }

    while (query.next()) {
        snapshot.items.append({query.value(0).toString(), query.value(1).toInt(), query.value(3).toInt()});
        snapshot.currentIndex = query.value(2).toInt();
    }
    if (query.exec(QStringLiteral("SELECT data FROM queue_folders WHERE id = 1")) && query.next())
        snapshot.folders = QJsonDocument::fromJson(query.value(0).toByteArray()).array();
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
