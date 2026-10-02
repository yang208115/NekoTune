#include "storage/database_session.h"
#include "app_paths.h"
#include <QFile>
#include <QLockFile>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

namespace nekotune {
DatabaseSession::DatabaseSession(const QString &databasePath, const QString &connectionName)
    : m_connectionName(connectionName.isEmpty() ? QStringLiteral("nekotune-song-store-%1")
                                                      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
                                                : connectionName) {
    m_ready = initialize(databasePath);
}

DatabaseSession::~DatabaseSession() {
    const QString connectionName = m_connectionName;
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

QString DatabaseSession::defaultDatabasePath() {
    const auto explicitPath = qEnvironmentVariable("NEKOTUNE_DB_PATH");
    return explicitPath.isEmpty() ? AppPaths::databasePath() : explicitPath;
}
namespace {
QString legacyDatabasePath() {
    const QDir current(QDir::currentPath()), app(QCoreApplication::applicationDirPath());
    QStringList candidates;
    if (current.exists("CMakeLists.txt") && current.exists("backend"))
        candidates.append(current.filePath("build/nekotune.sqlite3"));
    if (app.exists("../CMakeCache.txt"))
        candidates.append(app.filePath("../nekotune.sqlite3"));
    if (app.exists("CMakeCache.txt"))
        candidates.append(app.filePath("nekotune.sqlite3"));
    candidates.append(
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("nekotune.sqlite3"));
    const QDir data(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    candidates.append(data.filePath("NekoTune/NekoTune/nekotune.sqlite3"));
    candidates.append(data.filePath("NekoTune/NekoTune Backend/nekotune.sqlite3"));
    for (const auto &path : candidates)
        if (QFileInfo(path).isFile())
            return QFileInfo(path).absoluteFilePath();
    return {};
}
bool snapshotDatabase(const QString &source, const QString &target, QString &error) {
    const auto connection = "nekotune-migration-" + QUuid::createUuid().toString();
    const auto temporary = target + ".migration-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool ok = false;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(source);
        db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000");
        if (db.open()) {
            QSqlQuery query(db);
            auto escaped = temporary;
            escaped.replace("'", "''");
            ok = query.exec("VACUUM INTO '" + escaped + "'");
            if (!ok)
                error = query.lastError().text();
        } else
            error = db.lastError().text();
    }
    QSqlDatabase::removeDatabase(connection);
    if (ok) {
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(temporary);
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            if (db.open()) {
                QSqlQuery query(db);
                ok = query.exec("PRAGMA quick_check") && query.next() && query.value(0).toString() == "ok";
            } else
                ok = false;
            if (!ok)
                error = "Migrated database failed validation";
        }
        QSqlDatabase::removeDatabase(connection);
    }
    if (ok)
        ok = QFile::rename(temporary, target);
    if (!ok) {
        QFile::remove(temporary);
        if (error.isEmpty())
            error = "Cannot publish migrated database";
    }
    return ok;
}
} // namespace

bool DatabaseSession::initialize(const QString &databasePath) {
    m_databasePath = databasePath;
    if (databasePath == AppPaths::databasePath()) {
        if (!AppPaths::prepare(&m_error))
            return false;
        QLockFile lock(AppPaths::configFile("database-migration.lock"));
        if (!lock.tryLock(5000)) {
            setError("Database migration is busy");
            return false;
        }
        if (!QFileInfo::exists(databasePath) && qEnvironmentVariable("NEKOTUNE_HOME").isEmpty() &&
            qEnvironmentVariable("NEKOTUNE_DB_PATH").isEmpty()) {
            const auto source = legacyDatabasePath();
            if (!source.isEmpty() && !snapshotDatabase(source, databasePath, m_error))
                return false;
        }
    }
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

bool DatabaseSession::migrate() {
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS songs ("
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

    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS queue_items ("
                                   "position INTEGER PRIMARY KEY,"
                                   "path TEXT NOT NULL,"
                                   "song_id INTEGER NOT NULL,"
                                   "current_index INTEGER NOT NULL DEFAULT -1"
                                   ")"))) {
        setError(query.lastError().text());
        return false;
    }

    if (!query.exec("PRAGMA table_info(songs)")) {
        setError(query.lastError().text());
        return false;
    }
    bool hasSourceName = false, hasDuration = false;
    while (query.next()) {
        hasSourceName |= query.value(1).toString() == "source_name";
        hasDuration |= query.value(1).toString() == "duration_ms";
    }
    query.finish();
    if (!hasSourceName && !query.exec("ALTER TABLE songs ADD COLUMN source_name TEXT NOT NULL DEFAULT ''")) {
        setError(query.lastError().text());
        return false;
    }
    if (!m_db.transaction()) {
        setError(m_db.lastError().text());
        return false;
    }
    if (!hasDuration &&
        !query.exec("ALTER TABLE songs ADD COLUMN duration_ms INTEGER NOT NULL DEFAULT 0 CHECK(duration_ms >= 0)")) {
        setError(query.lastError().text());
        m_db.rollback();
        return false;
    }
    const QStringList statements{
        QStringLiteral("CREATE TABLE IF NOT EXISTS managed_resources (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "hash TEXT UNIQUE, original_path TEXT NOT NULL DEFAULT '', source_name TEXT NOT NULL "
                       "DEFAULT '', path TEXT NOT NULL DEFAULT '')"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS resource_sources (provider_hash TEXT PRIMARY KEY, "
                       "resource_id INTEGER NOT NULL REFERENCES managed_resources(id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS scan_ignored (hash TEXT PRIMARY KEY)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS playlists (id INTEGER PRIMARY KEY AUTOINCREMENT, name "
                       "TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS playlist_items (playlist_id INTEGER NOT NULL REFERENCES "
                       "playlists(id) ON DELETE CASCADE, song_id INTEGER NOT NULL REFERENCES songs(id), path "
                       "TEXT NOT NULL, position INTEGER NOT NULL, PRIMARY KEY (playlist_id, song_id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS tags (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT "
                       "NULL, normalized_name TEXT NOT NULL UNIQUE)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS song_tags (song_id INTEGER NOT NULL REFERENCES songs(id) "
                       "ON DELETE CASCADE, tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE, "
                       "PRIMARY KEY (song_id, tag_id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS song_paths (song_id INTEGER NOT NULL REFERENCES songs(id) "
                       "ON DELETE CASCADE, path TEXT NOT NULL, PRIMARY KEY (song_id, path))"),
        QStringLiteral("INSERT OR IGNORE INTO song_paths (song_id, path) SELECT id, first_path FROM songs"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS song_tags_by_tag ON song_tags(tag_id, song_id)"),
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

bool DatabaseSession::migrateFolders() {
    QSqlQuery query(m_db);
    auto exec = [&](const QString &sql) {
        if (query.exec(sql))
            return true;
        setError(query.lastError().text());
        return false;
    };
    if (m_db.tables().contains(QStringLiteral("queue_folders"))) {
        if (!exec(QStringLiteral("SELECT data FROM queue_folders WHERE id = 1")))
            return false;
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
            if (!query.exec()) {
                setError(query.lastError().text());
                return false;
            }
            const int playlistId = query.lastInsertId().toInt();
            query.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO playlist_items (playlist_id, song_id, path, position) "
                "SELECT :playlist_id, song_id, path, position FROM queue_items "
                "WHERE folder_id = :folder_id AND song_id IN (SELECT id FROM songs) ORDER BY position"));
            query.bindValue(QStringLiteral(":playlist_id"), playlistId);
            query.bindValue(QStringLiteral(":folder_id"), folder.value("id").toInt());
            if (!query.exec()) {
                setError(query.lastError().text());
                return false;
            }
        }
        if (!exec(QStringLiteral("DROP TABLE queue_folders")))
            return false;
    }
    if (!exec(QStringLiteral("PRAGMA table_info(queue_items)")))
        return false;
    bool hasFolder = false;
    while (query.next())
        hasFolder |= query.value(1).toString() == QStringLiteral("folder_id");
    query.finish();
    if (hasFolder && !exec(QStringLiteral("ALTER TABLE queue_items DROP COLUMN folder_id")))
        return false;
    return true;
}
bool DatabaseSession::begin() {
    if (m_transactionActive) {
        setError(QStringLiteral("Nested transactions are not supported"));
        return false;
    }
    if (!m_ready || !m_db.transaction()) {
        if (m_ready)
            setError(m_db.lastError().text());
        return false;
    }
    m_transactionActive = true;
    return true;
}
bool DatabaseSession::commit() {
    if (!m_transactionActive)
        return false;
    if (!m_db.commit()) {
        setError(m_db.lastError().text());
        return false;
    }
    m_transactionActive = false;
    return true;
}
void DatabaseSession::rollback() {
    if (m_transactionActive)
        m_db.rollback();
    m_transactionActive = false;
}
} // namespace nekotune
