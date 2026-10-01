#include "application/cover_service.h"
#include "infrastructure/lyrics_storage.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using namespace nekotune;

class CoverTest final : public QObject {
    Q_OBJECT
  private slots:
    void localCoverPriorityAndOffline() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        CoverService covers(std::make_unique<LyricsStorage>(directory.filePath("cache")));
        const auto audio = directory.filePath("song.wav");
        for (const auto &suffix : {".webp", ".png", ".jpeg", ".jpg"}) {
            const auto path = directory.filePath("song" + QString(suffix));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();
            QCOMPARE(covers.resolve(audio, "track"), QUrl::fromLocalFile(path).toString());
        }
        LyricsSnapshot state;
        state.trackId = "track";
        state.document = LyricsDocument{};
        state.document->coverUrl = "https://imge.kugou.com/remote.jpg";
        covers.updateLyrics(state);
        const auto local = QUrl::fromLocalFile(directory.filePath("song.jpg")).toString();
        QCOMPARE(covers.resolve(audio, "track"), local);
        state.offline = true;
        covers.updateLyrics(state);
        QCOMPARE(covers.resolve(audio, "track"), local);
        QCOMPARE(covers.resolve({}, "track"), QString());
        QCOMPARE(covers.resolve(directory.filePath("missing.wav"), "other"), QString());
    }

    void cachedAndSelectedCoversFollowTrackIdentity() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto cachePath = directory.filePath("cache");
        LyricsCache cache(cachePath);
        LyricsQuery query;
        query.trackId = "audio-hash";
        LyricsDocument document;
        document.source = "kugou";
        document.plainLyrics = "Cached lyrics";
        document.coverUrl = "https://imge.kugou.com/first.jpg";
        QVERIFY(cache.write(query, document));
        CoverService covers(std::make_unique<LyricsStorage>(cachePath));
        QCOMPARE(covers.resolve({}, query.trackId), document.coverUrl);
        QCOMPARE(covers.resolve({}, "different-hash"), QString());
        QCOMPARE(covers.resolve({}, {}), QString());
        QSignalSpy changed(&covers, &CoverService::changed);
        LyricsSnapshot state;
        state.trackId = query.trackId;
        state.state = "loading";
        covers.updateLyrics(state);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(covers.resolve({}, query.trackId), document.coverUrl);

        document.coverUrl = "https://imge.kugou.com/selected.jpg";
        QVERIFY(cache.write(query, document));
        state.document = document;
        covers.updateLyrics(state);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(covers.resolve({}, query.trackId), document.coverUrl);
        covers.updateLyrics(state);
        QCOMPARE(changed.count(), 1);
        state.trackId = "different-hash";
        state.document.reset();
        covers.updateLyrics(state);
        QCOMPARE(covers.resolve({}, state.trackId), QString());
        QCOMPARE(covers.resolve({}, query.trackId), document.coverUrl);

        state.offline = true;
        covers.updateLyrics(state);
        QCOMPARE(covers.resolve({}, query.trackId), QString());
        state.offline = false;
        covers.updateLyrics(state);
        QCOMPARE(covers.resolve({}, query.trackId), document.coverUrl);
        CoverService restored(std::make_unique<LyricsStorage>(cachePath));
        QCOMPARE(restored.resolve({}, query.trackId), document.coverUrl);

        document.coverUrl.clear();
        QVERIFY(cache.write(query, document));
        state.trackId = query.trackId;
        state.document = document;
        covers.updateLyrics(state);
        QCOMPARE(covers.resolve({}, query.trackId), QString());
    }
};
QTEST_GUILESS_MAIN(CoverTest)
#include "cover_test.moc"
