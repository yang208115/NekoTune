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
    void storesAndRestoresQueue();
    void migratesAndRestoresFolders();
};

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

void SongStoreTest::migratesAndRestoresFolders()
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
        db.close();
    }
    QSqlDatabase::removeDatabase("old-schema");
    {
        nekotune::SongStore store(path);
        QVERIFY2(store.isReady(), qPrintable(store.errorString()));
        auto snapshot = store.loadQueue();
        QCOMPARE(snapshot.items.size(), 1);
        QCOMPARE(snapshot.items[0].folderId, 0);
        snapshot.folders = QJsonArray{QJsonObject{{"id", 1}, {"parent_id", 0}, {"name", "Root"}},
                                     QJsonObject{{"id", 2}, {"parent_id", 1}, {"name", "Child"}}};
        snapshot.items[0].folderId = 2;
        QVERIFY(store.saveQueue(snapshot));
    }
    {
        nekotune::SongStore store(path);
        auto snapshot = store.loadQueue();
        QCOMPARE(snapshot.folders.size(), 2);
        QCOMPARE(snapshot.folders[1].toObject().value("parent_id").toInt(), 1);
        QCOMPARE(snapshot.items[0].folderId, 2);
        QCOMPARE(snapshot.currentIndex, 0);
        snapshot.items.clear();
        snapshot.currentIndex = -1;
        QVERIFY(store.saveQueue(snapshot));
    }
    nekotune::SongStore store(path);
    QCOMPARE(store.loadQueue().folders.size(), 2);
    QVERIFY(store.loadQueue().items.isEmpty());
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    SongStoreTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "song_store_test.moc"
