#include "core/player_queue.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest/QtTest>

class PlayerQueueTest final : public QObject {
    Q_OBJECT

private slots:
    void addAndRemoveQueueItems();
};

void PlayerQueueTest::addAndRemoveQueueItems()
{
    nekotune::PlayerQueue queue;

    const nekotune::SongMetadata firstMetadata {
        1,
        QStringLiteral("aaaaaaaa"),
        QStringLiteral("/music/first.mp3"),
        {},
        {},
        {},
    };
    const nekotune::SongMetadata secondMetadata {
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

    QJsonArray items = queue.toArray();
    QCOMPARE(items.size(), 3);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("first"));
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Custom Second"));
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("song_id")).toInt(), 1);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("song_hash")).toString(), firstMetadata.hash);
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("song_id")).toInt(), 2);
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("song_hash")).toString(), secondMetadata.hash);
    QCOMPARE(items.at(2).toObject().value(QStringLiteral("song_id")).toInt(), 2);
    QCOMPARE(items.at(2).toObject().value(QStringLiteral("song_hash")).toString(), secondMetadata.hash);
    QCOMPARE(queue.indexById(secondId), 1);

    QVERIFY(queue.setCurrentIndex(1));
    queue.markCurrent();
    QCOMPARE(queue.currentSongObject().value(QStringLiteral("id")).toInt(), secondId);
    QCOMPARE(queue.currentSongObject().value(QStringLiteral("queue_id")).toInt(), secondId);
    QCOMPARE(queue.currentSongObject().value(QStringLiteral("song_id")).toInt(), 2);
    QCOMPARE(queue.currentSongObject().value(QStringLiteral("song_hash")).toString(), secondMetadata.hash);
    QCOMPARE(queue.currentSongObject().value(QStringLiteral("lyrics")).toString(), secondMetadata.lyrics);

    nekotune::SongMetadata updatedSecond = secondMetadata;
    updatedSecond.customTitle = QStringLiteral("Updated Second");
    queue.updateSongMetadata(updatedSecond);
    QCOMPARE(queue.currentSongObject().value(QStringLiteral("title")).toString(), QStringLiteral("Updated Second"));

    QVERIFY(queue.removeAt(queue.indexById(firstId)));
    QCOMPARE(queue.currentIndex(), 0);

    items = queue.toArray();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Updated Second"));
    QCOMPARE(items.at(1).toObject().value(QStringLiteral("id")).toInt(), repeatedSecondId);

    QCOMPARE(queue.indexById(firstId), -1);
}

QTEST_APPLESS_MAIN(PlayerQueueTest)

#include "player_queue_test.moc"
