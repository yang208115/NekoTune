#include "domain/playback/player_queue.h"
#include "ipc/serialization/serialization.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest/QtTest>

class PlayerQueueTest final : public QObject {
    Q_OBJECT

  private slots:
    void addAndRemoveQueueItems();
};

// A song may occur more than once in the playback queue.
// Each occurrence needs its own stable ID while sharing the same song metadata.
// The serialized title must prefer the stored custom value over the source filename.
// Updating lyrics and metadata must reach every occurrence of that song.
// Removing an earlier row shifts the index without changing the active occurrence identity.
// This is why queue commands cannot substitute a library song ID for a queue ID.
void PlayerQueueTest::addAndRemoveQueueItems() {
    nekotune::PlayerQueue queue;

    const nekotune::SongMetadata firstMetadata{
        1, QStringLiteral("aaaaaaaa"), QStringLiteral("/music/first.mp3"), {}, {}, {},
    };
    const nekotune::SongMetadata secondMetadata{
        2,
        QStringLiteral("bbbbbbbb"),
        QStringLiteral("/music/second.wav"),
        QStringLiteral("Custom Second"),
        QStringLiteral("Second Artist"),
        QStringLiteral("second lyrics"),
    };

    const int firstId = queue.add(QStringLiteral("/music/first.mp3"), firstMetadata);
    const int secondId = queue.add(QStringLiteral("/music/second.wav"), secondMetadata);
    const int repeatedSecondId = queue.add(QStringLiteral("/music/second-copy.wav"), secondMetadata);

    QJsonArray items = nekotune::queueJson(queue);
    QCOMPARE(items.size(), 3);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("first"));
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("title")).toString(),
             QStringLiteral("Custom Second"));
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("song_id")).toInt(), 1);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("song_hash")).toString(), firstMetadata.hash);
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("song_id")).toInt(), 2);
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("song_hash")).toString(), secondMetadata.hash);
    QCOMPARE(items.at(2).toObject().value(QStringLiteral("song_id")).toInt(), 2);
    QCOMPARE(items.at(2).toObject().value(QStringLiteral("song_hash")).toString(), secondMetadata.hash);
    QCOMPARE(queue.indexById(secondId), 1);

    QVERIFY(queue.setCurrentIndex(1));
    queue.markCurrent();
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("id")).toInt(), secondId);
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("queue_id")).toInt(), secondId);
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("song_id")).toInt(), 2);
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("song_hash")).toString(),
             secondMetadata.hash);
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("lyrics")).toString(),
             secondMetadata.lyrics);

    nekotune::SongMetadata updatedSecond = secondMetadata;
    updatedSecond.customTitle = QStringLiteral("Updated Second");
    queue.updateSongMetadata(updatedSecond);
    QCOMPARE(nekotune::currentSongJson(queue).value(QStringLiteral("title")).toString(),
             QStringLiteral("Updated Second"));

    QVERIFY(queue.removeAt(queue.indexById(firstId)));
    QCOMPARE(queue.currentIndex(), 0);

    items = nekotune::queueJson(queue);
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("title")).toString(),
             QStringLiteral("Updated Second"));
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("id")).toInt(), repeatedSecondId);

    QCOMPARE(queue.indexById(firstId), -1);
}

QTEST_APPLESS_MAIN(PlayerQueueTest)

#include "player_queue_test.moc"
