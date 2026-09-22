#include "storage/song_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class SongStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void usesBuildDatabasePathInDevelopment();
    void storesAndUpdatesSongMetadata();
    void storesAndRestoresQueue();
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

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    SongStoreTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "song_store_test.moc"
