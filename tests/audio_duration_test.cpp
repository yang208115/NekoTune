#include "application/queue_service.h"
#include "infrastructure/audio_duration.h"
#include "ipc/serialization.h"
#include "runtime/library_scanner.h"
#include "support/store_fixture.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace nekotune;
namespace {
QByteArray wav(int milliseconds = 1500) {
  constexpr quint32 sampleRate = 8000;
  const quint32 size = milliseconds * sampleRate / 1000 * 2;
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.writeRawData("RIFF", 4);
  stream << quint32(36 + size);
  stream.writeRawData("WAVEfmt ", 8);
  stream << quint32(16) << quint16(1) << quint16(1) << sampleRate
         << quint32(sampleRate * 2) << quint16(2) << quint16(16);
  stream.writeRawData("data", 4);
  stream << size;
  bytes.append(QByteArray(size, '\0'));
  return bytes;
}
QString hash(const QByteArray &bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
void write(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(bytes), bytes.size());
}
} // namespace

class AudioDurationTest final : public QObject {
  Q_OBJECT
private slots:
  void probesOffThreadAndPreservesManagedFile() {
    QTemporaryDir dir;
    const auto path = dir.filePath("原曲.wav");
    const auto bytes = wav();
    write(path, bytes);
    StoreFixture store(dir.filePath("db.sqlite3"));
    MusicDirectory music(store.db, dir.filePath("music"));
    ImportExecutor imports;
    imports.setMapper(
        [&](const ImportedFile &file) { return music.manage(file); });
    std::optional<ImportedFile> inspected;
    imports.inspect(path, [&](Result<ImportedFile> result) {
      QVERIFY(result);
      inspected = result.value();
    });
    QTRY_VERIFY(inspected.has_value());
    QCOMPARE(inspected->durationMs, 1500);
    QCOMPARE(inspected->hash, hash(bytes));
    QVERIFY(QFileInfo(inspected->path).isSymLink());
    const auto imported = store.library.importFile(*inspected);
    QVERIFY(imported);
    QCOMPARE(imported.value().durationMs, 1500);
    QCOMPARE(toJson(store.library.snapshot())["songs"]
                 .toArray()[0]
                 .toObject()["duration_ms"]
                 .toInteger(),
             1500);
    std::atomic_bool cancelled{true};
    QCOMPARE(readAudioDuration(path, cancelled), 0);
    cancelled = false;
    const auto corrupt = dir.filePath("broken.mp3");
    write(corrupt, "not audio");
    QCOMPARE(readAudioDuration(corrupt, cancelled), 0);
    QCOMPARE(readAudioDuration(dir.filePath("missing.wav"), cancelled), 0);
    QFile original(path);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), bytes);
    // A failed probe during re-import must not erase the known value.
    inspected->durationMs = 0;
    const auto repeated = store.library.importFile(*inspected);
    QVERIFY(repeated);
    QCOMPARE(repeated.value().durationMs, 1500);
  }
  void migratesLegacySchemaAndPreservesDurationAcrossRestart() {
    QTemporaryDir dir;
    const auto path = dir.filePath("db.sqlite3");
    {
      auto db = QSqlDatabase::addDatabase("QSQLITE", "duration-legacy");
      db.setDatabaseName(path);
      QVERIFY(db.open());
      QSqlQuery query(db);
      QVERIFY(query.exec(
          "CREATE TABLE songs (id INTEGER PRIMARY KEY, hash TEXT UNIQUE NOT "
          "NULL, "
          "first_path TEXT NOT NULL, custom_title TEXT NOT NULL DEFAULT '', "
          "artist TEXT NOT NULL DEFAULT '', lyrics TEXT NOT NULL DEFAULT '', "
          "created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT "
          "CURRENT_TIMESTAMP)"));
      QVERIFY(query.exec(
          "INSERT INTO songs(id,hash,first_path,custom_title,artist,lyrics) "
          "VALUES(7,'legacy','/music/song.wav','Saved "
          "title','Artist','Lyrics')"));
    }
    QSqlDatabase::removeDatabase("duration-legacy");
    {
      StoreFixture store(path);
      QVERIFY2(store.isReady(), qPrintable(store.errorString()));
      QCOMPARE(store.songById(7)->durationMs, 0);
      QVERIFY(store.library.backfillDuration(7, "legacy", 192000));
      const auto song = store.songById(7);
      QCOMPARE(song->durationMs, 192000);
      QCOMPARE(song->customTitle, QString("Saved title"));
      QCOMPARE(song->artist, QString("Artist"));
      QCOMPARE(song->lyrics, QString("Lyrics"));
    }
    StoreFixture restored(path);
    QVERIFY(restored.isReady());
    QCOMPARE(restored.songById(7)->durationMs, 192000);
    QCOMPARE(restored.songs().first().durationMs, 192000);
  }
  void backfillsLegacyAndManagedSongsWithoutChangingCollections() {
    QTemporaryDir dir;
    const auto path = dir.filePath("outside.wav");
    const auto managedRoot = dir.filePath("music");
    write(path, wav());
    write(managedRoot + "/new.wav", wav(2500));
    StoreFixture store(dir.filePath("db.sqlite3"));
    const auto song =
        store.getOrCreateSong(hash(wav()), path, "Custom", "Artist");
    QVERIFY(song);
    QVERIFY(store.updateMetadata(song->id, "Custom", "Artist", "Lyrics",
                                 QStringList{"Favorite"}));
    const auto missing =
        store.getOrCreateSong("missing", dir.filePath("missing.wav"));
    QVERIFY(missing);
    const auto changed = dir.filePath("changed.wav");
    write(changed, wav(3500));
    const auto stale = store.getOrCreateSong(hash(wav(4500)), changed);
    QVERIFY(stale);
    const int playlist = store.createPlaylist("Saved list");
    QVERIFY(store.addPlaylistSong(playlist, {path, song->id}));
    QVERIFY(store.saveQueue({{{path, song->id}}, 0}));
    QueueService queue(store.queueRepo, store.songRepo, store.db);
    connect(&store.library, &LibraryService::durationUpdated, &queue,
            &QueueService::updateMetadata);
    MusicDirectory music(store.db, managedRoot);
    ImportExecutor imports;
    CommandScheduler commands;
    LibraryScanner scanner(imports, music, store.library, commands);
    QSignalSpy finished(&scanner, &LibraryScanner::finished);
    QVERIFY(scanner.start());
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(store.songById(song->id)->durationMs, 1500);
    QCOMPARE(store.songById(song->id)->firstPath, path);
    QCOMPARE(store.songById(song->id)->customTitle, QString("Custom"));
    QCOMPARE(store.songById(song->id)->lyrics, QString("Lyrics"));
    QCOMPARE(store.songTags()[song->id].first().name, QString("Favorite"));
    QCOMPARE(store.songById(missing->id)->durationMs, 0);
    QCOMPARE(store.songById(stale->id)->durationMs, 0);
    QCOMPARE(store.songRepo.songs().last().durationMs, 2500);
    QCOMPARE(store.loadQueue().currentIndex, 0);
    QCOMPARE(store.loadQueue().items.size(), 1);
    QCOMPARE(store.loadQueue().items.first().path, path);
    QCOMPARE(store.playlistById(playlist)->items.first().songId, song->id);
    QCOMPARE(
        queueJson(queue.queue()).first().toObject()["duration_ms"].toInteger(),
        1500);
    QVERIFY(scanner.start());
    QTRY_COMPARE(finished.count(), 2);
    QCOMPARE(store.songs().size(), 4);
    QCOMPARE(store.songById(song->id)->durationMs, 1500);
    imports.shutdown();
  }
  void rejectsStaleResultsAndRollsBackFailedWrites() {
    QTemporaryDir dir;
    StoreFixture store(dir.filePath("db.sqlite3"));
    auto song = store.getOrCreateSong("hash", "/music/song.wav");
    QVERIFY(song);
    QVERIFY(store.library.backfillDuration(song->id, "wrong-hash", 1000));
    QCOMPARE(store.songById(song->id)->durationMs, 0);
    QSignalSpy changed(&store.library, &LibraryService::durationUpdated);
    QSqlQuery query(store.db.database());
    QVERIFY(query.exec(
        "CREATE TRIGGER fail_duration BEFORE UPDATE OF duration_ms ON songs "
        "BEGIN SELECT RAISE(ABORT,'fixture failure'); END"));
    QVERIFY(!store.library.backfillDuration(song->id, "hash", 1000));
    QCOMPARE(store.songById(song->id)->durationMs, 0);
    QCOMPARE(changed.count(), 0);
    QVERIFY(query.exec("DROP TRIGGER fail_duration"));
    QVERIFY(store.library.backfillDuration(song->id, "hash", 1000));
    QCOMPARE(changed.count(), 1);
    QVERIFY(store.library.backfillDuration(song->id, "hash", 2000));
    QCOMPARE(store.songById(song->id)->durationMs, 1000);
    QVERIFY(store.songRepo.erase(song->id));
    QVERIFY(store.library.backfillDuration(song->id, "hash", 1000));
    QVERIFY(store.songs().isEmpty());
    QVERIFY(query.exec("SELECT 1 FROM scan_ignored WHERE hash='hash'"));
    QVERIFY(query.next());
  }
};
QTEST_GUILESS_MAIN(AudioDurationTest)
#include "audio_duration_test.moc"
