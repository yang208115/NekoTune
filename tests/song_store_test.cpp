#include "storage/song_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QJsonObject>
#include <QtTest/QtTest>

class SongStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void usesBuildDatabasePathInDevelopment();
    void storesAndUpdatesSongMetadata();
    void storesOriginalTranscription();
    void backsUpAsrTasksAndMigratesExistingDatabase();
    void storesAndRestoresQueue();
    void migratesLegacyQueue();
    void migratesFoldersToPlaylists();
    void storesIndependentPlaylists();
    void rejectsInvalidFolderMigration();
};

void SongStoreTest::storesOriginalTranscription()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("songs.sqlite3"));
    const QByteArray original = R"({"file_url":"signed-url","properties":{"channels":[0,1]},"transcripts":[],"extra":{"confidence":0.92}})";
    {
        nekotune::SongStore store(path);
        QVERIFY(store.getOrCreateSong(QStringLiteral("hash"), QStringLiteral("song.wav")));
        QVERIFY(store.saveTranscription(QStringLiteral("hash"), original));
        QCOMPARE(store.transcription(QStringLiteral("hash")), original);
        QVERIFY(!store.saveTranscription(QStringLiteral("missing-song"), original));
        QVERIFY(!store.saveTranscription(QStringLiteral("hash"), "not json"));
        QCOMPARE(store.transcription(QStringLiteral("hash")), original);
    }
    nekotune::SongStore restored(path);
    QCOMPARE(restored.transcription(QStringLiteral("hash")), original);
    QVERIFY(restored.transcription(QStringLiteral("other-hash")).isEmpty());
    QVERIFY(restored.saveTranscription(QStringLiteral("hash"), "{\"replacement\":true}"));
    QCOMPARE(restored.transcription(QStringLiteral("hash")), QByteArray("{\"replacement\":true}"));
}

void SongStoreTest::backsUpAsrTasksAndMigratesExistingDatabase()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("songs.sqlite3");
    const QString connection = QStringLiteral("asr-task-backup-test");
    {
        nekotune::SongStore store(path, connection);
        QVERIFY(store.getOrCreateSong("song-hash", "song.wav"));
        QVERIFY(store.saveTranscription("song-hash", "{\"original\":true}"));
        QSqlQuery query(QSqlDatabase::database(connection));
        // Model the schema from before task backups were added.
        QVERIFY(query.exec("DROP TABLE asr_tasks"));
    }
    {
        nekotune::SongStore store(path, connection);
        QVERIFY2(store.isReady(), qPrintable(store.errorString()));
        QVERIFY(store.saveAsrTask("task-one", "song-hash", "fun-asr", "ja"));
        QVERIFY(store.saveAsrTask("task-two", "song-hash", "qwen3-asr-flash-filetrans", "en"));
        QVERIFY(store.saveAsrTask("task-one", "song-hash", "paraformer-v2", "auto"));
        QVERIFY(!store.saveAsrTask("orphan", "missing-song", "fun-asr", "ja"));
        QVERIFY(!store.saveAsrTask("", "song-hash", "fun-asr", "ja"));
        QVERIFY(!store.saveAsrTask("task-three", "song-hash", "", "ja"));
        QVERIFY(!store.saveAsrTask("task-three", "song-hash", "fun-asr", ""));
    }
    {
        nekotune::SongStore store(path, connection);
        QVERIFY(store.isReady());
        QCOMPARE(store.transcription("song-hash"), QByteArray("{\"original\":true}"));
        QSqlQuery query(QSqlDatabase::database(connection));
        QVERIFY(query.exec("SELECT task_id, song_hash, model, language, created_at FROM asr_tasks ORDER BY task_id"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("task-one"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("song-hash"));
        QCOMPARE(query.value(2).toString(), QStringLiteral("fun-asr"));
        QCOMPARE(query.value(3).toString(), QStringLiteral("ja"));
        QVERIFY(!query.value(4).toString().isEmpty());
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("task-two"));
        QCOMPARE(query.value(2).toString(), QStringLiteral("qwen3-asr-flash-filetrans"));
        QCOMPARE(query.value(3).toString(), QStringLiteral("en"));
        QVERIFY(!query.next());
    }
}

void SongStoreTest::usesBuildDatabasePathInDevelopment()
{
    QCOMPARE(nekotune::SongStore::defaultDatabasePath(),
             QFileInfo(QDir(QCoreApplication::applicationDirPath())
                           .filePath(QStringLiteral("../nekotune.sqlite3")))
                 .absoluteFilePath());
}

void SongStoreTest::storesAndRestoresQueue()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    nekotune::SongStore store(tempDir.filePath(QStringLiteral("queue.sqlite3")));
    QVERIFY(store.isReady());

    const auto song = store.getOrCreateSong(QStringLiteral("queue-hash"),
                                             QStringLiteral("/music/queue.mp3"));
    QVERIFY(song.has_value());
    QVERIFY(store.saveQueue({
        {{QStringLiteral("/music/queue.mp3"), song->id},
         {QStringLiteral("/music/queue-copy.mp3"), song->id}},
        1,
    }));

    const auto snapshot = store.loadQueue();
    QCOMPARE(snapshot.items.size(), 2);
    QCOMPARE(snapshot.items.at(1).path, QStringLiteral("/music/queue-copy.mp3"));
    QCOMPARE(snapshot.currentIndex, 1);
}

void SongStoreTest::storesAndUpdatesSongMetadata()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString databasePath = tempDir.filePath(QStringLiteral("library.sqlite3"));
    nekotune::SongStore store(databasePath);
    QVERIFY2(store.isReady(), qPrintable(store.errorString()));

    const auto first = store.getOrCreateSong(QStringLiteral("abc123"), QStringLiteral("/music/song.mp3"));
    QVERIFY(first.has_value());
    QCOMPARE(first->id, 1);
    QCOMPARE(first->hash, QStringLiteral("abc123"));
    QCOMPARE(first->firstPath, QStringLiteral("/music/song.mp3"));

    const auto repeated = store.getOrCreateSong(QStringLiteral("abc123"), QStringLiteral("/music/song-copy.mp3"));
    QVERIFY(repeated.has_value());
    QCOMPARE(repeated->id, first->id);
    QCOMPARE(repeated->firstPath, QStringLiteral("/music/song.mp3"));

    const auto updated = store.updateMetadata(first->id,
                                             QStringLiteral("My Title"),
                                             QStringLiteral("My Artist"),
                                             QStringLiteral("line 1\nline 2"));
    QVERIFY(updated.has_value());
    QCOMPARE(updated->customTitle, QStringLiteral("My Title"));
    QCOMPARE(updated->artist, QStringLiteral("My Artist"));
    QCOMPARE(updated->lyrics, QStringLiteral("line 1\nline 2"));

    const auto fetched = store.songById(first->id);
    QVERIFY(fetched.has_value());
    QCOMPARE(fetched->customTitle, QStringLiteral("My Title"));
    QCOMPARE(fetched->artist, QStringLiteral("My Artist"));
    QCOMPARE(fetched->lyrics, QStringLiteral("line 1\nline 2"));

    const auto songs = store.songs();
    QCOMPARE(songs.size(), 1);
    QCOMPARE(songs.at(0).id, first->id);
    QCOMPARE(songs.at(0).customTitle, QStringLiteral("My Title"));
}

void SongStoreTest::migratesLegacyQueue()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("old.sqlite3");
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "old-schema");
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("CREATE TABLE queue_items (position INTEGER PRIMARY KEY, path TEXT NOT NULL, song_id INTEGER NOT NULL, current_index INTEGER NOT NULL DEFAULT -1)"));
        QVERIFY(query.exec("INSERT INTO queue_items VALUES (0, '/music/old.wav', 1, 0)"));
    }
    QSqlDatabase::removeDatabase("old-schema");
    nekotune::SongStore store(path);
    QVERIFY2(store.isReady(), qPrintable(store.errorString()));
    QCOMPARE(store.loadQueue().items.size(), 1);
    QCOMPARE(store.loadQueue().currentIndex, 0);
    QVERIFY(store.playlists().isEmpty());
}

void SongStoreTest::migratesFoldersToPlaylists()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("folders.sqlite3");
    {
        nekotune::SongStore store(path);
        QVERIFY(store.getOrCreateSong("first", "/music/first.wav"));
        QVERIFY(store.getOrCreateSong("second", "/music/second.wav"));
        QVERIFY(store.saveQueue({{{"/music/first.wav", 1}, {"/music/second.wav", 2}, {"/music/copy.wav", 2}}, 1}));
    }
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "folder-schema");
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("ALTER TABLE queue_items ADD COLUMN folder_id INTEGER NOT NULL DEFAULT 0"));
        QVERIFY(query.exec("UPDATE queue_items SET folder_id = 2 WHERE song_id = 2"));
        QVERIFY(query.exec("CREATE TABLE queue_folders (id INTEGER PRIMARY KEY, data TEXT NOT NULL)"));
        QVERIFY(query.exec(R"(INSERT INTO queue_folders VALUES (1, '[{"id":1,"parent_id":0,"name":"Rock"},{"id":2,"parent_id":1,"name":"Live"},{"id":3,"parent_id":0,"name":"Empty"}]'))"));
    }
    QSqlDatabase::removeDatabase("folder-schema");
    for (int attempt = 0; attempt < 2; ++attempt) {
        nekotune::SongStore store(path);
        QVERIFY2(store.isReady(), qPrintable(store.errorString()));
        const auto playlists = store.playlists();
        QCOMPARE(playlists.size(), 3);
        QCOMPARE(playlists[0].name, QString("Rock"));
        QVERIFY(playlists[0].items.isEmpty());
        QCOMPARE(playlists[1].name, QString("Rock / Live"));
        QCOMPARE(playlists[1].items.size(), 1);
        QCOMPARE(playlists[1].items[0].songId, 2);
        QCOMPARE(playlists[1].items[0].path, QString("/music/second.wav"));
        QVERIFY(playlists[2].items.isEmpty());
        QCOMPARE(store.loadQueue().items.size(), 3);
        QCOMPARE(store.loadQueue().currentIndex, 1);
    }
}

void SongStoreTest::storesIndependentPlaylists()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("playlists.sqlite3");
    int rock = 0, live = 0;
    {
        nekotune::SongStore store(path);
        QVERIFY(store.isReady());
        QVERIFY(store.getOrCreateSong("first", "/music/first.wav"));
        QVERIFY(store.getOrCreateSong("second", "/music/second.wav"));
        rock = store.createPlaylist(" Rock ");
        live = store.createPlaylist("Live");
        QVERIFY(rock > 0 && live > rock);
        QCOMPARE(store.createPlaylist(" "), 0);
        QCOMPARE(store.createPlaylist(QString(129, 'a')), 0);
        QVERIFY(!store.renamePlaylist(rock, ""));
        QVERIFY(!store.renamePlaylist(999, "Missing"));
        QVERIFY(!store.addPlaylistSong(999, {"/music/first.wav", 1}));
        QVERIFY(!store.addPlaylistSong(rock, {"/music/missing.wav", 999}));
        QVERIFY(store.addPlaylistSong(rock, {"/music/second.wav", 2}));
        QVERIFY(store.addPlaylistSong(rock, {"/music/first.wav", 1}));
        QVERIFY(store.addPlaylistSong(rock, {"/music/copy.wav", 2}));
        QVERIFY(store.addPlaylistSong(live, {"/music/first.wav", 1}));
        QCOMPARE(store.playlistById(rock)->items.size(), 2);
        QCOMPARE(store.playlistById(rock)->items[0].path, QString("/music/copy.wav"));
        QVERIFY(store.renamePlaylist(rock, " Favorites "));
        QVERIFY(store.saveQueue({{{"/music/first.wav", 1}}, 0}));
        QVERIFY(store.saveQueue({}));
        QCOMPARE(store.playlistById(rock)->items.size(), 2);
    }
    {
        nekotune::SongStore store(path);
        QCOMPARE(store.playlists().size(), 2);
        QCOMPARE(store.playlistById(rock)->name, QString("Favorites"));
        QCOMPARE(store.playlistById(rock)->items[0].songId, 2);
        QCOMPARE(store.playlistById(rock)->items[1].songId, 1);
        QVERIFY(store.removePlaylistSong(rock, 1));
        QVERIFY(!store.removePlaylistSong(rock, 1));
        QCOMPARE(store.playlistById(live)->items.size(), 1);
        QVERIFY(store.saveQueue({{{"/music/first.wav", 1}}, 0}));
        QVERIFY(store.deletePlaylist(live));
        QVERIFY(!store.playlistById(live));
        QVERIFY(!store.deletePlaylist(live));
        QCOMPARE(store.loadQueue().items.size(), 1);
        QCOMPARE(store.songs().size(), 2);
        QVERIFY(store.createPlaylist("New") > live);
    }
}

void SongStoreTest::rejectsInvalidFolderMigration()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("invalid.sqlite3");
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "invalid-schema");
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("CREATE TABLE queue_folders (id INTEGER PRIMARY KEY, data TEXT NOT NULL)"));
        QVERIFY(query.exec(R"(INSERT INTO queue_folders VALUES (1, '[{"id":1,"parent_id":1,"name":"Cycle"}]'))"));
    }
    QSqlDatabase::removeDatabase("invalid-schema");
    for (int attempt = 0; attempt < 2; ++attempt) {
        nekotune::SongStore store(path);
        QVERIFY(!store.isReady());
        QVERIFY(store.errorString().contains("hierarchy"));
    }
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "inspect-schema");
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QVERIFY(db.tables().contains("queue_folders"));
        QVERIFY(!db.tables().contains("playlists"));
    }
    QSqlDatabase::removeDatabase("inspect-schema");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    SongStoreTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "song_store_test.moc"
