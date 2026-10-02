#include "support/store_fixture.h"
#include "app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class StoreFixtureTest final : public QObject {
    Q_OBJECT

  private slots:
    void usesManagedDatabasePath();
    void storesAndUpdatesSongMetadata();
    void remembersImportedPathsAcrossRestart();
    void preservesLegacyAsrTables();
    void storesAndRestoresQueue();
    void migratesLegacyQueue();
    void migratesFoldersToPlaylists();
    void storesIndependentPlaylists();
    void storesMultipleTagsWithoutChangingCollections();
    void deletesSongsAtomicallyAcrossCollections();
    void rejectsInvalidFolderMigration();
};

// A fresh schema must not recreate retired ASR feature tables.
// An existing schema can still contain historical ASR records.
// Reopening must leave those records untouched rather than treating
// feature retirement as permission to delete user data.
// Explicit fixtures distinguish absence-by-default from preservation-on-upgrade.
void StoreFixtureTest::preservesLegacyAsrTables() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("songs.sqlite3"));
    const QString connection = QStringLiteral("legacy-asr-preservation-test");
    {
        nekotune::StoreFixture store(path, connection);
        QVERIFY(store.getOrCreateSong(QStringLiteral("hash"), QStringLiteral("song.wav")));
        QSqlQuery query(QSqlDatabase::database(connection));
        QVERIFY(query.exec("SELECT name FROM sqlite_master WHERE type='table' "
                           "AND name IN ('asr_tasks', 'song_transcriptions')"));
        QVERIFY(!query.next());
        QVERIFY(query.exec("CREATE TABLE asr_tasks (task_id TEXT PRIMARY KEY, song_hash TEXT)"));
        QVERIFY(query.exec(
            "CREATE TABLE song_transcriptions (song_hash TEXT PRIMARY KEY, transcription_json TEXT)"));
        QVERIFY(query.exec("INSERT INTO asr_tasks VALUES ('task-one', 'hash')"));
        QVERIFY(query.exec("INSERT INTO song_transcriptions VALUES ('hash', '{\"old\":true}')"));
    }
    {
        nekotune::StoreFixture store(path, connection);
        QVERIFY2(store.isReady(), qPrintable(store.errorString()));
        QSqlQuery query(QSqlDatabase::database(connection));
        QVERIFY(query.exec("SELECT task_id FROM asr_tasks"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("task-one"));
        QVERIFY(query.exec("SELECT transcription_json FROM song_transcriptions"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("{\"old\":true}"));
    }
}

// The storage fixture must follow the same path policy as the application.
// This prevents tests from legitimizing a second database-directory convention.
// The comparison deliberately uses AppPaths rather than a host-specific literal.
void StoreFixtureTest::usesManagedDatabasePath() {
    QCOMPARE(nekotune::StoreFixture::defaultDatabasePath(),
             nekotune::AppPaths::databasePath());
}

// The same song can occupy two queue positions with distinct chosen paths.
// Persisting must preserve both entries and the selected occurrence index.
// Deduplicating queue records by song ID would destroy this contract.
// Selection is restored from durable order rather than decoder state.
void StoreFixtureTest::storesAndRestoresQueue() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    nekotune::StoreFixture store(tempDir.filePath(QStringLiteral("queue.sqlite3")));
    QVERIFY(store.isReady());

    const auto song = store.getOrCreateSong(QStringLiteral("queue-hash"), QStringLiteral("/music/queue.mp3"));
    QVERIFY(song.has_value());
    QVERIFY(store.saveQueue({
        {{QStringLiteral("/music/queue.mp3"), song->id}, {QStringLiteral("/music/queue-copy.mp3"), song->id}},
        1,
    }));

    const auto snapshot = store.loadQueue();
    QCOMPARE(snapshot.items.size(), 2);
    QCOMPARE(snapshot.items.at(1).path, QStringLiteral("/music/queue-copy.mp3"));
    QCOMPARE(snapshot.currentIndex, 1);
}

// Repeated audio content must resolve to the original library song ID.
// Its original first path is historical metadata rather than a moving locator.
// User title/artist/lyrics edits must survive direct lookup and list snapshots.
// A second import path cannot create another song for the same hash.
// The result also validates the repository/service metadata write boundary.
void StoreFixtureTest::storesAndUpdatesSongMetadata() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString databasePath = tempDir.filePath(QStringLiteral("library.sqlite3"));
    nekotune::StoreFixture store(databasePath);
    QVERIFY2(store.isReady(), qPrintable(store.errorString()));

    const auto first = store.getOrCreateSong(QStringLiteral("abc123"), QStringLiteral("/music/song.mp3"));
    QVERIFY(first.has_value());
    QCOMPARE(first->id, 1);
    QCOMPARE(first->hash, QStringLiteral("abc123"));
    QCOMPARE(first->firstPath, QStringLiteral("/music/song.mp3"));

    const auto repeated =
        store.getOrCreateSong(QStringLiteral("abc123"), QStringLiteral("/music/song-copy.mp3"));
    QVERIFY(repeated.has_value());
    QCOMPARE(repeated->id, first->id);
    QCOMPARE(repeated->firstPath, QStringLiteral("/music/song.mp3"));

    const auto updated = store.updateMetadata(first->id, QStringLiteral("My Title"),
                                              QStringLiteral("My Artist"), QStringLiteral("line 1\nline 2"));
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

// Reimporting from a new path must preserve manually entered metadata.
// Remember that alternate location so it remains usable after reopening storage.
// Library-only import must not create playback queue occurrences.
// Both single-song and bulk path APIs must see the remembered location.
// The two scopes force a real close/reopen instead of testing only memory state.
void StoreFixtureTest::remembersImportedPathsAcrossRestart() {
    QTemporaryDir dir;
    const auto database = dir.filePath(QStringLiteral("paths.sqlite3"));
    int songId = 0;
    {
        nekotune::StoreFixture store(database);
        const auto song =
            store.getOrCreateSong(QStringLiteral("same-content"), QStringLiteral("/old/song.mp3"),
                                  QStringLiteral("Song"), QStringLiteral("Singer"));
        QVERIFY(song);
        QCOMPARE(song->customTitle, QStringLiteral("Song"));
        QCOMPARE(song->artist, QStringLiteral("Singer"));
        songId = song->id;
        const auto imported =
            store.getOrCreateSong(QStringLiteral("same-content"), QStringLiteral("/new/song.mp3"),
                                  QStringLiteral("Overwritten"), QStringLiteral("Other"));
        QVERIFY(imported);
        QCOMPARE(imported->id, songId);
        QCOMPARE(imported->customTitle, QStringLiteral("Song"));
        QCOMPARE(imported->artist, QStringLiteral("Singer"));
        QVERIFY(store.pathsForSong(songId).contains(QStringLiteral("/new/song.mp3")));
        QVERIFY(store.loadQueue().items.isEmpty());
    }
    {
        nekotune::StoreFixture store(database);
        QVERIFY2(store.isReady(), qPrintable(store.errorString()));
        QCOMPARE(store.songs().size(), 1);
        QVERIFY(store.pathsForSong(songId).contains(QStringLiteral("/new/song.mp3")));
        QVERIFY(store.songPaths().value(songId).contains(QStringLiteral("/new/song.mp3")));
    }
}

// Supply the old queue schema directly instead of generating a current schema.
// Migration must retain its record order and selected index.
// An old live queue does not implicitly become a saved playlist.
// This guards the compatibility boundary between queue state and collections.
void StoreFixtureTest::migratesLegacyQueue() {
    QTemporaryDir dir;
    const auto path = dir.filePath("old.sqlite3");
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "old-schema");
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("CREATE TABLE queue_items (position INTEGER PRIMARY KEY, path TEXT NOT NULL, "
                           "song_id INTEGER NOT NULL, current_index INTEGER NOT NULL DEFAULT -1)"));
        QVERIFY(query.exec("INSERT INTO queue_items VALUES (0, '/music/old.wav', 1, 0)"));
    }
    QSqlDatabase::removeDatabase("old-schema");
    nekotune::StoreFixture store(path);
    QVERIFY2(store.isReady(), qPrintable(store.errorString()));
    QCOMPARE(store.loadQueue().items.size(), 1);
    QCOMPARE(store.loadQueue().currentIndex, 0);
    QVERIFY(store.playlists().isEmpty());
}

// Legacy nested folders become named independent playlists.
// The path of ancestor names is flattened while empty folders are retained.
// Repeated songs collapse only within playlist membership, not the live queue.
// Original queue selection and occurrence count must remain unchanged.
// Reopen twice to ensure migration is idempotent and does not duplicate playlists.
void StoreFixtureTest::migratesFoldersToPlaylists() {
    QTemporaryDir dir;
    const auto path = dir.filePath("folders.sqlite3");
    {
        nekotune::StoreFixture store(path);
        QVERIFY(store.getOrCreateSong("first", "/music/first.wav"));
        QVERIFY(store.getOrCreateSong("second", "/music/second.wav"));
        QVERIFY(store.saveQueue(
            {{{"/music/first.wav", 1}, {"/music/second.wav", 2}, {"/music/copy.wav", 2}}, 1}));
    }
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "folder-schema");
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("ALTER TABLE queue_items ADD COLUMN folder_id INTEGER NOT NULL DEFAULT 0"));
        QVERIFY(query.exec("UPDATE queue_items SET folder_id = 2 WHERE song_id = 2"));
        QVERIFY(query.exec("CREATE TABLE queue_folders (id INTEGER PRIMARY KEY, data TEXT NOT NULL)"));
        QVERIFY(query.exec(
            R"(INSERT INTO queue_folders VALUES (1, '[{"id":1,"parent_id":0,"name":"Rock"},{"id":2,"parent_id":1,"name":"Live"},{"id":3,"parent_id":0,"name":"Empty"}]'))"));
    }
    QSqlDatabase::removeDatabase("folder-schema");
    for (int attempt = 0; attempt < 2; ++attempt) {
        nekotune::StoreFixture store(path);
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

// One library song can belong to several independently ordered playlists.
// Adding it again updates its path without appending another membership.
// Queue replacement/clear must leave those saved collections untouched.
// Removing from one playlist must not affect another or erase the library song.
// Renames and deletion persist through a real storage reopen.
// The test protects collection identity from the retired queue-folder model.
void StoreFixtureTest::storesIndependentPlaylists() {
    QTemporaryDir dir;
    const auto path = dir.filePath("playlists.sqlite3");
    int rock = 0, live = 0;
    {
        nekotune::StoreFixture store(path);
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
        nekotune::StoreFixture store(path);
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

// Tag assignments use shared case-normalized identities across songs.
// Omitted tag updates preserve assignments while an explicit set replaces them.
// Reimporting the audio must keep its classification and user edits.
// Invalid tag input must roll back the title change in the same metadata save.
// Global rename/delete updates associations without changing playlist/queue membership.
// Reopening verifies the classification lives in storage rather than UI memory.
void StoreFixtureTest::storesMultipleTagsWithoutChangingCollections() {
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("tags.sqlite3"));
    int firstId = 0;
    int secondId = 0;
    int nightId = 0;
    {
        nekotune::StoreFixture store(path);
        QVERIFY2(store.isReady(), qPrintable(store.errorString()));
        const auto first =
            store.getOrCreateSong(QStringLiteral("same-hash"), QStringLiteral("/music/first.wav"));
        const auto second =
            store.getOrCreateSong(QStringLiteral("other-hash"), QStringLiteral("/music/second.wav"));
        QVERIFY(first && second);
        firstId = first->id;
        secondId = second->id;
        const int playlistA = store.createPlaylist(QStringLiteral("A"));
        const int playlistB = store.createPlaylist(QStringLiteral("B"));
        QVERIFY(store.addPlaylistSong(playlistA, {QStringLiteral("/music/first.wav"), firstId}));
        QVERIFY(store.addPlaylistSong(playlistB, {QStringLiteral("/music/first.wav"), firstId}));
        QVERIFY2(store.updateMetadata(
                     firstId, QStringLiteral("First"), {}, {},
                     QStringList{QStringLiteral(" Rock "), QStringLiteral("rock"), QStringLiteral("夜晚")}),
                 qPrintable(store.errorString()));
        QVERIFY(store.updateMetadata(secondId, {}, {}, {}, QStringList{QStringLiteral("ROCK")}));
        QCOMPARE(store.tags().size(), 2);
        QCOMPARE(store.songTags().value(firstId).size(), 2);
        QCOMPARE(store.songTags().value(secondId).size(), 1);
        QVERIFY(store.updateMetadata(firstId, QStringLiteral("First again"), {}, {}));
        QCOMPARE(store.songTags().value(firstId).size(), 2);
        const auto repeated =
            store.getOrCreateSong(QStringLiteral("same-hash"), QStringLiteral("/music/copy.wav"));
        QCOMPARE(repeated->id, firstId);
        QCOMPARE(store.songTags().value(repeated->id).size(), 2);
        QVERIFY(!store.updateMetadata(firstId, QStringLiteral("Wrong"), {}, {},
                                      QStringList{QString(65, QChar('x'))}));
        QCOMPARE(store.songById(firstId)->customTitle, QStringLiteral("First again"));
        QCOMPARE(store.songTags().value(firstId).size(), 2);
        QVERIFY(!store.createTag(QStringLiteral("rOcK")));
        for (const auto &tag : store.tags()) {
            if (tag.name == QStringLiteral("夜晚"))
                nightId = tag.id;
        }
        QVERIFY(nightId > 0);
        QVERIFY(store.renameTag(nightId, QStringLiteral("深夜")));
        QVERIFY(!store.renameTag(nightId, QStringLiteral("rock")));
        QCOMPARE(store.songTags().value(firstId).last().name, QStringLiteral("深夜"));
        QVERIFY(store.saveQueue({{{QStringLiteral("/music/first.wav"), firstId}}, 0}));
        QVERIFY(store.saveQueue({}));
        QCOMPARE(store.playlists().size(), 2);
        QCOMPARE(store.playlistById(playlistA)->items.size(), 1);
        QCOMPARE(store.playlistById(playlistB)->items.size(), 1);
    }
    {
        nekotune::StoreFixture store(path);
        QVERIFY2(store.isReady(), qPrintable(store.errorString()));
        QCOMPARE(store.songTags().value(firstId).size(), 2);
        QCOMPARE(store.songTags().value(secondId).size(), 1);
        QVERIFY(store.deleteTag(nightId));
        QCOMPARE(store.songTags().value(firstId).size(), 1);
        QCOMPARE(store.songs().size(), 2);
        QCOMPARE(store.playlists().size(), 2);
        QCOMPARE(store.loadQueue().items.size(), 0);
    }
}

// Library deletion removes every selected song's playlist and queue references.
// Invalid batches must reject before a partial change becomes durable.
// An injected write failure exercises transaction rollback after work begins.
// The saved replacement queue must match the intended survivors exactly.
// The test isolates logical deletion from optional managed filesystem cleanup.
// Persistence is verified by opening the database again after the operation.
void StoreFixtureTest::deletesSongsAtomicallyAcrossCollections() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto filePath = dir.filePath(QStringLiteral("keep-on-disk.wav"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("audio bytes");
    file.close();

    const auto dbPath = dir.filePath(QStringLiteral("delete.sqlite3"));
    int retainedId = 0;
    int deletedId = 0;
    int playlistId = 0;
    {
        nekotune::StoreFixture store(dbPath, QStringLiteral("delete-rollback-test"));
        QVERIFY2(store.isReady(), qPrintable(store.errorString()));
        const auto retained = store.getOrCreateSong(QStringLiteral("retained"), filePath);
        const auto deleted = store.getOrCreateSong(QStringLiteral("deleted"), filePath);
        QVERIFY(retained && deleted);
        retainedId = retained->id;
        deletedId = deleted->id;
        playlistId = store.createPlaylist(QStringLiteral("Favorites"));
        QVERIFY(playlistId > 0);
        QVERIFY(store.addPlaylistSong(playlistId, {filePath, retainedId}));
        QVERIFY(store.addPlaylistSong(playlistId, {filePath, deletedId}));
        QVERIFY(store.updateMetadata(deletedId, {}, {}, {}, QStringList{QStringLiteral("Delete me")}));
        QVERIFY(store.saveQueue({{{filePath, deletedId}, {filePath, retainedId}}, 0}));

        const nekotune::QueueSnapshot remaining{{{filePath, retainedId}}, 0};
        QVERIFY(!store.deleteSongs({deletedId, 9999}, remaining));
        QCOMPARE(store.songs().size(), 2);
        QCOMPARE(store.playlistById(playlistId)->items.size(), 2);
        QCOMPARE(store.loadQueue().items.size(), 2);
        QVERIFY(!store.deleteSongs({deletedId, deletedId}, remaining));
        QVERIFY(!store.deleteSongs({deletedId}, {{{filePath, deletedId}}, 0}));
        QSqlQuery query(QSqlDatabase::database(QStringLiteral("delete-rollback-test")));
        QVERIFY(query.exec(QStringLiteral("CREATE TRIGGER reject_second_delete BEFORE DELETE ON songs "
                                          "WHEN OLD.id = %1 BEGIN SELECT RAISE(ABORT, 'blocked'); END")
                               .arg(deletedId)));
        QVERIFY(!store.deleteSongs({retainedId, deletedId}, {}));
        QCOMPARE(store.songs().size(), 2);
        QCOMPARE(store.playlistById(playlistId)->items.size(), 2);
        QCOMPARE(store.loadQueue().items.size(), 2);
        QVERIFY(query.exec(QStringLiteral("DROP TRIGGER reject_second_delete")));
        QVERIFY2(store.deleteSongs({deletedId}, remaining), qPrintable(store.errorString()));
        QVERIFY(!store.songById(deletedId));
        QCOMPARE(store.songTags().value(deletedId).size(), 0);
        QCOMPARE(store.playlistById(playlistId)->items.size(), 1);
        QCOMPARE(store.playlistById(playlistId)->items[0].songId, retainedId);
        QCOMPARE(store.loadQueue().items.size(), 1);
        QCOMPARE(store.loadQueue().items[0].songId, retainedId);
        QCOMPARE(store.loadQueue().currentIndex, 0);
        QVERIFY(QFileInfo::exists(filePath));
    }
    {
        nekotune::StoreFixture store(dbPath);
        QVERIFY2(store.isReady(), qPrintable(store.errorString()));
        QCOMPARE(store.songs().size(), 1);
        QCOMPARE(store.playlistById(playlistId)->items.size(), 1);
        QCOMPARE(store.loadQueue().items.size(), 1);
        const auto importedAgain = store.getOrCreateSong(QStringLiteral("deleted"), filePath);
        QVERIFY(importedAgain);
        QVERIFY(importedAgain->id != deletedId);
    }
}

// Broken parent references or cycles cannot be flattened safely.
// Reject the migration instead of silently inventing a playlist hierarchy.
// The failure must retain legacy source data under the migration rollback.
// This protects recoverability when an old database contains malformed folder state.
void StoreFixtureTest::rejectsInvalidFolderMigration() {
    QTemporaryDir dir;
    const auto path = dir.filePath("invalid.sqlite3");
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "invalid-schema");
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("CREATE TABLE queue_folders (id INTEGER PRIMARY KEY, data TEXT NOT NULL)"));
        QVERIFY(
            query.exec(R"(INSERT INTO queue_folders VALUES (1, '[{"id":1,"parent_id":1,"name":"Cycle"}]'))"));
    }
    QSqlDatabase::removeDatabase("invalid-schema");
    for (int attempt = 0; attempt < 2; ++attempt) {
        nekotune::StoreFixture store(path);
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

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    StoreFixtureTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "song_store_test.moc"
