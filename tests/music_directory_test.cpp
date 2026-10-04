#include "app_paths.h"
#include "application/lyrics/cover_service.h"
#include "i18n.h"
#include "infrastructure/lyrics/lyrics_storage.h"
#include "infrastructure/library/music_directory.h"
#include "infrastructure/library/audio_reference.h"
#include "infrastructure/library/import_executor.h"
#include "application/lyrics/lyrics_service.h"
#include "infrastructure/lyrics/sidecar_store.h"
#include "runtime/backend_runtime.h"
#include "support/store_fixture.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QNetworkReply>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>
#include <cstring>

using namespace nekotune;
namespace {
void write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
class Rpc {
  public:
    QLocalSocket socket;
    QByteArray buffer;
    int id = 0;
    bool connect(const QString &path) {
        socket.connectToServer(path);
        return socket.waitForConnected(3000);
    }
    QJsonObject call(const QString &method, const QJsonObject &params = {}) {
        const auto next = ++id;
        socket.write(QJsonDocument(QJsonObject{{"id", next}, {"method", method}, {"params", params}})
                         .toJson(QJsonDocument::Compact) +
                     '\n');
        socket.flush();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            QCoreApplication::processEvents();
            buffer += socket.readAll();
            while (buffer.contains('\n')) {
                auto line = buffer.left(buffer.indexOf('\n'));
                buffer.remove(0, line.size() + 1);
                const auto object = QJsonDocument::fromJson(line).object();
                if (object.value("id").toInt() == next)
                    return object;
            }
            QTest::qWait(1);
        }
        return {};
    }
};
class ImageReply final : public QNetworkReply {
  public:
    ImageReply(const QNetworkRequest &request, const QByteArray &body, int delay, int status) : m_body(body) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(delay, this, [this] {
            if (m_finished)
                return;
            m_available = true;
            emit readyRead();
            m_finished = true;
            setFinished(true);
            emit finished();
        });
    }
    void abort() override {
        if (m_finished)
            return;
        m_finished = true;
        setError(OperationCanceledError, "Cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return (m_available ? m_body.size() - m_offset : 0) + QNetworkReply::bytesAvailable();
    }

  protected:
    qint64 readData(char *data, qint64 maximum) override {
        if (!m_available)
            return 0;
        const auto count = qMin(maximum, qint64(m_body.size() - m_offset));
        if (!count)
            return -1;
        std::memcpy(data, m_body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

  private:
    QByteArray m_body;
    qint64 m_offset = 0;
    bool m_available = false, m_finished = false;
};
class ImageNetwork final : public QNetworkAccessManager {
  public:
    QByteArray body;
    int delay = 1, status = 200, calls = 0;

  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++calls;
        auto *reply = new ImageReply(request, body, delay, status);
        reply->setParent(this);
        return reply;
    }
};
} // namespace

class MusicDirectoryTest final : public QObject {
    Q_OBJECT
  private:
    QTemporaryDir m_profile;
  private slots:
    void init() {
        QVERIFY(m_profile.isValid());
        const auto directory = m_profile.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
        qputenv("NEKOTUNE_HOME", (directory + "/music").toUtf8());
        qputenv("NEKOTUNE_DB_PATH", (directory + "/player.sqlite3").toUtf8());
        qputenv("NEKOTUNE_SOCKET", (directory + "/backend.sock").toUtf8());
        qunsetenv("KUGOU_ACCOUNT_API_KEY");
        qunsetenv("KUGOU_ACCOUNT_API_KEY_FILE");
    }
    // External audio remains the user's source of truth; importing creates a managed link.
    // The same content must reuse its existing song number even through another source path.
    // Canonical targets and original source names must survive this indirection.
    // Downloaded files reserve numbers through the same persistent allocator.
    // Reopening the directory checks that numbering is database state, not a process counter.
    // These assertions distinguish deduplication by content from deduplication by filename.
    void linksAudioAndReusesPersistentNumbers() {
        const auto source = m_profile.filePath("original.flac");
        write(source, "one");
        const auto dbPath = qEnvironmentVariable("NEKOTUNE_DB_PATH");
        QString path;
        {
            StoreFixture store(dbPath);
            QVERIFY2(store.isReady(), qPrintable(store.errorString()));
            MusicDirectory music(store.db);
            auto result = music.manage({source, hash("one"), "Original song"});
            QVERIFY(result);
            path = result.value().path;
            QCOMPARE(path, QDir(AppPaths::musicDirectory()).filePath("000001/000001.flac"));
            QVERIFY(QFileInfo(path).isSymLink());
            QCOMPARE(QFileInfo(path).symLinkTarget(), source);
            auto song = store.library.importFile(result.value());
            QVERIFY(song);
            QCOMPARE(song.value().sourceName, QString("Original song"));
            QVERIFY(store.songRepo.erase(song.value().id));
            QVERIFY(music.ignored(hash("one")).value());
            auto repeated = music.manage({source, hash("one"), "Another name"});
            QVERIFY(repeated);
            QCOMPARE(repeated.value().path, path);
            QVERIFY(store.library.importFile(repeated.value()));
            QVERIFY(!music.ignored(hash("one")).value());
            auto reserved = music.reserveDownload("provider-hash", "Downloaded title");
            QVERIFY(reserved);
            QCOMPARE(reserved.value(), QDir(AppPaths::musicDirectory()).filePath("000002/000002"));
            QCOMPARE(music.reserveDownload("provider-hash", "Renamed title").value(), reserved.value());
        }
        QVERIFY(QFile::remove(source));
        const auto replacement = m_profile.filePath("replacement.flac");
        write(replacement, "one");
        StoreFixture restored(dbPath);
        MusicDirectory music(restored.db);
        auto repaired = music.manage({replacement, hash("one"), "Replacement"});
        QVERIFY(repaired);
        QCOMPARE(repaired.value().path, path);
        QCOMPARE(QFileInfo(path).symLinkTarget(), replacement);
        QVERIFY(music.reserveDownload("another-provider", "Other").value().endsWith("000003/000003"));
    }
    void nativeAudioLinksExposeRealBytes() {
        const auto source = m_profile.filePath("native-links/原歌曲.flac");
        const auto target = m_profile.filePath("native-links/linked.flac");
        write(source, "native link audio bytes");
        if (!createAudioSymlink(source, target))
            QSKIP("Native file symlink creation is unavailable for this user/filesystem");
        QVERIFY(QFileInfo(target).isSymbolicLink());
        QCOMPARE(read(target), QByteArray("native link audio bytes"));
        QVERIFY(QFile::remove(target));
        QCOMPARE(read(source), QByteArray("native link audio bytes"));
        QVERIFY(createAudioSymlink(source, target));
        StoreFixture store(m_profile.filePath("native-link-availability.sqlite3"));
        const auto imported = store.library.importFile({target, hash("native link audio bytes"), "Native link"});
        QVERIFY(imported);
        QVERIFY(!store.library.availablePath(imported.value().id).isEmpty());
        QVERIFY(QFile::remove(source));
        QVERIFY(store.library.availablePath(imported.value().id).isEmpty());
        QCOMPARE(store.library.snapshot().songs.size(), 1);
        QVERIFY(store.library.snapshot().songs.first().path.isEmpty());
    }
    void referenceFallbackKeepsPlaybackAndAssetsSeparate() {
        const auto source = m_profile.filePath("外部音乐/歌曲.flac");
        write(source, "referenced audio");
        write(m_profile.filePath("外部音乐/歌曲.lrc"), "[00:00.00]Source lyrics");
        StoreFixture store(qEnvironmentVariable("NEKOTUNE_DB_PATH"));
        MusicDirectory music(store.db, {}, [](const QString &, const QString &) { return false; });
        const auto audioHash = hash("referenced audio");
        const auto imported = music.manage({source, audioHash, "External title"});
        QVERIFY(imported);
        QCOMPARE(imported.value().path, source);
        QVERIFY(store.library.importFile(imported.value()));
        const auto base = music.baseFor(audioHash);
        QVERIFY(base.endsWith("000001/000001"));
        QVERIFY(!QFileInfo::exists(base + ".flac"));
        const auto reference = readAudioReference(base + ".audio.json");
        QVERIFY(reference);
        QCOMPARE(reference.value().path, source);
        QCOMPARE(reference.value().hash, audioHash);
        write(base + ".lrc", "[00:00.00]Managed lyrics");
        write(base + ".png", "managed cover");
        CoverService covers(std::make_unique<LyricsStorage>());
        covers.setManagedBaseResolver([&](const QString &key) { return music.baseFor(key); });
        QCOMPARE(QUrl(covers.resolve(source, audioHash)).toLocalFile(), base + ".png");
        LyricsService lyrics({}, std::make_unique<LyricsStorage>());
        lyrics.setOffline(true);
        QString selected;
        connect(&lyrics, &LyricsService::changed, this, [&](const LyricsSnapshot &state) {
            if (state.document)
                selected = state.document->syncedLyrics;
        });
        LyricsQuery query;
        query.trackId = audioHash;
        lyrics.load(query, source, {}, 1, false, false, base);
        QCOMPARE(selected, QString("[00:00.00]Managed lyrics"));
        QVERIFY(QFile::remove(base + ".lrc"));
        lyrics.load(query, source, {}, 2, false, false, base);
        QCOMPARE(selected, QString("[00:00.00]Source lyrics"));
        QVERIFY(QFile::remove(source));
        QVERIFY(store.library.snapshot().songs.first().path.isEmpty());
        QCOMPARE(QUrl(covers.resolve({}, audioHash)).toLocalFile(), base + ".png");
        const auto replacement = m_profile.filePath("新位置/歌曲.flac");
        write(replacement, "referenced audio");
        QSqlQuery sql(store.db.database());
        QVERIFY(sql.exec("CREATE TRIGGER reject_repair BEFORE UPDATE ON managed_resources "
                         "BEGIN SELECT RAISE(ABORT, 'reject repair'); END"));
        QVERIFY(!music.manage({replacement, audioHash, "Replacement"}));
        QCOMPARE(readAudioReference(base + ".audio.json").value().path, source);
        QVERIFY(sql.exec("DROP TRIGGER reject_repair"));
        auto repaired = music.manage({replacement, audioHash, "Replacement"});
        QVERIFY(repaired);
        QVERIFY(store.library.importFile(repaired.value()));
        QCOMPARE(store.library.snapshot().songs.first().path, replacement);
        QCOMPARE(music.baseFor(audioHash), base);
        QCOMPARE(readAudioReference(base + ".audio.json").value().path, replacement);
        QVERIFY(QFile::remove(replacement));
        const auto downloaded = base + ".flac";
        write(downloaded, "referenced audio");
        QVERIFY(sql.exec("CREATE TRIGGER reject_owned_audio BEFORE UPDATE ON managed_resources "
                         "BEGIN SELECT RAISE(ABORT, 'reject owned audio'); END"));
        QVERIFY(!music.manage({downloaded, audioHash, "Downloaded"}));
        QCOMPARE(readAudioReference(base + ".audio.json").value().path, replacement);
        QCOMPARE(read(downloaded), QByteArray("referenced audio"));
        QVERIFY(sql.exec("DROP TRIGGER reject_owned_audio"));
        auto owned = music.manage({downloaded, audioHash, "Downloaded"});
        QVERIFY(owned);
        QCOMPARE(owned.value().path, downloaded);
        QVERIFY(!QFileInfo::exists(base + ".audio.json"));
        QVERIFY(store.db.begin());
        {
            auto removal = music.stageRemoval({audioHash});
            QVERIFY(removal);
            store.db.rollback();
        }
        QCOMPARE(read(downloaded), QByteArray("referenced audio"));
    }
    void referencesScanAndRestoreWithoutAcceptingChangedAudio() {
        const auto source = m_profile.filePath("reference-scan/song.wav");
        write(source, "original referenced bytes");
        QString referencePath;
        {
            StoreFixture store(qEnvironmentVariable("NEKOTUNE_DB_PATH"));
            MusicDirectory music(store.db, {}, [](const QString &, const QString &) { return false; });
            QVERIFY(music.manage({source, hash("original referenced bytes"), "Recovered title"}));
            referencePath = music.baseFor(hash("original referenced bytes")) + ".audio.json";
        }
        ImportExecutor imports;
        bool discovered = false;
        QStringList paths;
        imports.discover(AppPaths::musicDirectory(), [&](QStringList found) {
            paths = std::move(found);
            discovered = true;
        });
        QTRY_VERIFY(discovered);
        QCOMPARE(paths, QStringList{referencePath});
        std::optional<Result<ImportedFile>> inspected;
        imports.inspectUnmanaged(referencePath, [&](Result<ImportedFile> file) { inspected = std::move(file); });
        QTRY_VERIFY(inspected.has_value());
        QVERIFY(*inspected);
        QCOMPARE(inspected->value().path, source);
        QCOMPARE(inspected->value().managedReferencePath, referencePath);
        StoreFixture restored(m_profile.filePath("restored-reference.sqlite3"));
        MusicDirectory music(restored.db);
        const auto registered = music.manage(inspected->value());
        QVERIFY(registered);
        QCOMPARE(registered.value().path, source);
        QCOMPARE(music.baseFor(registered.value().hash) + ".audio.json", referencePath);
        QVERIFY(music.reserveDownload("new-provider", "New").value().endsWith("000002/000002"));
        write(source, "different audio at the same path");
        inspected.reset();
        imports.inspectUnmanaged(referencePath, [&](Result<ImportedFile> file) { inspected = std::move(file); });
        QTRY_VERIFY(inspected.has_value());
        QVERIFY(!*inspected);
        QCOMPARE(inspected->error().message, QString("Referenced audio content has changed"));
        write(referencePath, "not JSON");
        inspected.reset();
        imports.inspectUnmanaged(referencePath, [&](Result<ImportedFile> file) { inspected = std::move(file); });
        QTRY_VERIFY(inspected.has_value());
        QVERIFY(!*inspected);
        write(referencePath, QByteArray(64 * 1024 + 1, 'x'));
        QVERIFY(!readAudioReference(referencePath));
    }
    void referenceCleanupRollsBackAndNeverDeletesExternalAudio() {
        const auto source = m_profile.filePath("reference-cleanup/song.mp3");
        write(source, "preserve external audio");
        StoreFixture store(qEnvironmentVariable("NEKOTUNE_DB_PATH"));
        MusicDirectory music(store.db, {}, [](const QString &, const QString &) { return false; });
        const auto audioHash = hash("preserve external audio");
        const auto file = music.manage({source, audioHash, "External"});
        QVERIFY(file);
        QVERIFY(store.library.importFile(file.value()));
        const auto base = music.baseFor(audioHash);
        write(base + ".lrc", "managed lyrics");
        write(QFileInfo(base).dir().filePath("notes.txt"), "retain unrelated file");
        QVERIFY(store.db.begin());
        {
            auto staged = music.stageRemoval({audioHash});
            QVERIFY(staged);
            QVERIFY(!QFileInfo::exists(base + ".audio.json"));
            QCOMPARE(read(source), QByteArray("preserve external audio"));
            store.db.rollback();
        }
        QVERIFY(QFileInfo::exists(base + ".audio.json"));
        QCOMPARE(read(base + ".lrc"), QByteArray("managed lyrics"));
        QVERIFY(store.db.begin());
        auto staged = music.stageRemoval({audioHash});
        QVERIFY(staged);
        QVERIFY(store.db.commit());
        QVERIFY(staged.value()->commit().isEmpty());
        QVERIFY(!QFileInfo::exists(base + ".audio.json"));
        QVERIFY(!QFileInfo::exists(base + ".lrc"));
        QCOMPARE(read(source), QByteArray("preserve external audio"));
        QCOMPARE(read(QFileInfo(base).dir().filePath("notes.txt")), QByteArray("retain unrelated file"));
    }
    void referenceRegistrationFailureAndTamperingPreserveFiles() {
        const auto source = m_profile.filePath("reference-errors/song.mp3");
        write(source, "external bytes");
        StoreFixture store(qEnvironmentVariable("NEKOTUNE_DB_PATH"));
        MusicDirectory music(store.db, {}, [](const QString &, const QString &) { return false; });
        QSqlQuery sql(store.db.database());
        QVERIFY(sql.exec("CREATE TRIGGER reject_reference BEFORE UPDATE ON managed_resources "
                         "BEGIN SELECT RAISE(ABORT, 'reject reference'); END"));
        QVERIFY(!music.manage({source, hash("external bytes"), "External"}));
        QVERIFY(!QFileInfo::exists(AppPaths::musicDirectory() + "/000001/000001.audio.json"));
        QCOMPARE(read(source), QByteArray("external bytes"));
        QVERIFY(sql.exec("DROP TRIGGER reject_reference"));
        const auto file = music.manage({source, hash("external bytes"), "External"});
        QVERIFY(file);
        const auto base = music.baseFor(file.value().hash);
        const auto other = m_profile.filePath("other.mp3");
        write(other, "unrelated audio");
        QVERIFY(writeAudioReference(base + ".audio.json", {other, file.value().hash, "Tampered"}));
        QVERIFY(!music.stageRemoval({file.value().hash}));
        QVERIFY(QFileInfo::exists(base + ".audio.json"));
        QCOMPARE(read(source), QByteArray("external bytes"));
        QCOMPARE(read(other), QByteArray("unrelated audio"));
    }
    void referenceStartupScanRespectsDeletionAndIpcCleanup() {
        const auto source = m_profile.filePath("reference-runtime/song.mp3");
        write(source, "runtime referenced audio");
        const auto audioHash = hash("runtime referenced audio");
        QString base;
        {
            StoreFixture store(qEnvironmentVariable("NEKOTUNE_DB_PATH"));
            MusicDirectory music(store.db, {}, [](const QString &, const QString &) { return false; });
            QVERIFY(music.manage({source, audioHash, "Runtime reference"}));
            base = music.baseFor(audioHash);
        }
        BackendRuntime runtime;
        QVERIFY(runtime.start());
        Rpc peer;
        QVERIFY(peer.connect(qEnvironmentVariable("NEKOTUNE_SOCKET")));
        auto songs = [&] {
            return peer.call("library.list").value("data").toObject()
                .value("library").toObject().value("songs").toArray();
        };
        QTRY_COMPARE(songs().size(), 1);
        const auto id = songs().first().toObject().value("song_id").toInt();
        QCOMPARE(songs().first().toObject().value("path").toString(), source);
        QCOMPARE(peer.call("library.delete", {{"song_ids", QJsonArray{id}}})
                     .value("status").toString(), QString("ok"));
        QVERIFY(QFileInfo::exists(base + ".audio.json"));
        runtime.stop();
        QVERIFY(runtime.start());
        Rpc restarted;
        QVERIFY(restarted.connect(qEnvironmentVariable("NEKOTUNE_SOCKET")));
        QTest::qWait(150);
        QVERIFY(restarted.call("library.list").value("data").toObject()
                    .value("library").toObject().value("songs").toArray().isEmpty());
        const auto imported = restarted.call("library.import", {{"path", source}});
        QCOMPARE(imported.value("status").toString(), QString("ok"));
        const auto newId = imported.value("data").toObject().value("song_id").toInt();
        write(base + ".lrc", "managed lyrics");
        QCOMPARE(restarted.call("library.delete", {{"song_ids", QJsonArray{newId}}, {"clean_files", true}})
                     .value("status").toString(), QString("ok"));
        QVERIFY(!QFileInfo::exists(base + ".audio.json"));
        QVERIFY(!QFileInfo::exists(base + ".lrc"));
        QCOMPARE(read(source), QByteArray("runtime referenced audio"));
        runtime.stop();
    }
    // Only validated lyric and image content should become app-owned sidecars.
    // Delayed replies simulate a request whose track revision is no longer current.
    // Changing identity while a reply is pending must prevent an obsolete file write.
    // KRC and LRC persistence are checked separately because their representations differ.
    // An existing symlink must not turn a sidecar save into a write outside the managed root.
    // The fake transport controls timing without requiring an online lyrics account.
    void savesRealLyricsAndArtworkAndRejectsStaleRequests() {
        const auto original = m_profile.filePath("external.wav");
        write(original, "audio");
        StoreFixture db(qEnvironmentVariable("NEKOTUNE_DB_PATH"));
        MusicDirectory music(db.db);
        auto audio = music.manage({original, hash("audio"), "External"});
        QVERIFY(audio);
        const auto base = music.baseFor(hash("audio"));
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        ImageNetwork network;
        QBuffer imageBuffer(&network.body);
        imageBuffer.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&imageBuffer, "PNG"));
        SidecarStore assets(&network);
        CoverService covers(std::make_unique<LyricsStorage>(m_profile.filePath("cover-cache")));
        connect(&assets, &SidecarStore::saved, &covers, &CoverService::assetsUpdated);
        QSignalSpy saved(&assets, &SidecarStore::saved);
        QSignalSpy failures(&assets, &SidecarStore::failed);
        LyricsDocument document;
        document.source = "kugou";
        document.syncedLyrics = "[00:00.00]First";
        document.krcLyrics = "[0,1000]<0,1000,0>First";
        document.coverUrl = "https://imge.kugou.com/first.png";
        assets.setCurrent(hash("audio"), 1, false);
        assets.save(hash("audio"), 1, base, document);
        QTRY_COMPARE(saved.count(), 2);
        const auto initialCover = covers.resolve(audio.value().path, hash("audio"));
        QCOMPARE(QUrl(initialCover).toLocalFile(), base + ".png");
        QCOMPARE(read(base + ".lrc"), document.syncedLyrics.toUtf8());
        QVERIFY(!QFileInfo(base + ".lrc").isSymLink());
        QVERIFY(!QFileInfo(base + ".png").isSymLink());
        QVERIFY(QFileInfo(audio.value().path).isSymLink());
        QVERIFY(!QFileInfo(m_profile.filePath("external.lrc")).exists());
        LyricsQuery query;
        query.trackId = hash("audio");
        auto local = LyricsStorage(m_profile.filePath("cache")).readLocal(query, audio.value().path);
        QVERIFY(local && local.value());
        QCOMPARE(local.value()->krcLyrics, document.krcLyrics);
        network.delay = 100;
        assets.setCurrent(hash("audio"), 2, false);
        assets.save(hash("audio"), 2, base, document);
        assets.setCurrent(hash("audio"), 3, false);
        document.krcLyrics.clear();
        document.syncedLyrics = "[00:00.00]Replacement";
        document.coverUrl.clear();
        assets.save(hash("audio"), 3, base, document);
        QTest::qWait(150);
        QVERIFY(!QFileInfo(base + ".krc").exists());
        QVERIFY(!QFileInfo(base + ".png").exists());
        QCOMPARE(read(base + ".lrc"), document.syncedLyrics.toUtf8());
        network.delay = 1;
        network.status = 500;
        document.coverUrl = "https://imge.kugou.com/failure.png";
        assets.setCurrent(hash("audio"), 4, false);
        assets.save(hash("audio"), 4, base, document);
        QTRY_VERIFY(failures.count() > 0);
        QCOMPARE(read(original), QByteArray("audio"));
        const auto calls = network.calls;
        assets.setCurrent(hash("audio"), 5, true);
        assets.save(hash("audio"), 5, base, document);
        QCOMPARE(network.calls, calls);
        QVERIFY(QFileInfo(base + ".lrc").isFile());
        network.status = 200;
        assets.setCurrent(hash("audio"), 6, false);
        assets.save(hash("audio"), 6, base, document);
        QTRY_VERIFY(QFileInfo(base + ".png").isFile());
        QVERIFY(covers.resolve(audio.value().path, hash("audio")) != initialCover);
        document.coverUrl.clear();
        document.source = "lrclib";
        assets.setCurrent(hash("audio"), 7, false);
        assets.save(hash("audio"), 7, base, document);
        QVERIFY(QFileInfo(base + ".png").isFile());
    }
    // File cleanup owns numbered managed assets, not the external sources behind audio links.
    // The fixture includes external lyrics, unrelated notes and an image link to expose this boundary.
    // Deleting a managed link must leave its target and source-side metadata intact.
    // Collection removal still updates the queue and song database together.
    // The subsequent scan must respect the deletion marker rather than resurrecting the song.
    // Explicit reimport is the operation allowed to make that content visible again.
    void removesManagedFilesAndPreservesExternalOriginals() {
        const auto root = AppPaths::musicDirectory();
        const auto source = m_profile.filePath("cleanup-original/song.mp3");
        write(source, "external song to retain");
        write(source + ".lrc", "external lyrics to retain");
        BackendRuntime runtime;
        QVERIFY(runtime.start());
        Rpc peer;
        QVERIFY(peer.connect(qEnvironmentVariable("NEKOTUNE_SOCKET")));
        auto imported = peer.call("library.import", {{"path", source}});
        QCOMPARE(imported.value("status").toString(), QString("ok"));
        const auto linkedPath = imported.value("data").toObject().value("path").toString();
        const int linkedId = imported.value("data").toObject().value("song_id").toInt();
        QVERIFY(QFileInfo(linkedPath).isSymLink());
        const auto linkedBase = QFileInfo(linkedPath).absolutePath() + '/' + QFileInfo(linkedPath).completeBaseName();
        write(linkedBase + ".lrc", "managed lyrics");
        const auto outsideCover = m_profile.filePath("cleanup-original/cover.png");
        write(outsideCover, "external cover to retain");
        QVERIFY(QFile::link(outsideCover, linkedBase + ".png"));
        const auto ownedPath = root + "/000100/000100.mp3";
        write(ownedPath, "downloaded audio to remove");
        imported = peer.call("library.import", {{"path", ownedPath}});
        QCOMPARE(imported.value("status").toString(), QString("ok"));
        const int ownedId = imported.value("data").toObject().value("song_id").toInt();
        write(root + "/000100/000100.krc", "managed krc");
        write(root + "/000100/000100.jpg", "managed cover");
        write(root + "/000100/notes.txt", "unrelated file to retain");
        QCOMPARE(peer.call("queue.add", {{"path", ownedPath}}).value("status").toString(), QString("ok"));
        const auto playlist = peer.call("playlist.create", {{"name", "Cleanup"}}).value("data").toObject().value("playlist_id").toInt();
        QCOMPARE(peer.call("playlist.add", {{"id", playlist}, {"song_id", linkedId}}).value("status").toString(), QString("ok"));
        const auto result = peer.call("library.delete", {{"song_ids", QJsonArray{linkedId, ownedId}}, {"clean_files", true}});
        QCOMPARE(result.value("status").toString(), QString("ok"));
        QCOMPARE(result.value("data").toObject().value("deleted_count").toInt(), 2);
        QVERIFY(result.value("data").toObject().value("cleanup_errors").toArray().isEmpty());
        QVERIFY(!QFileInfo(linkedPath).isSymLink());
        QVERIFY(!QFileInfo::exists(linkedBase + ".lrc"));
        QVERIFY(!QFileInfo(linkedBase + ".png").isSymLink());
        QVERIFY(!QFileInfo::exists(ownedPath));
        QVERIFY(!QFileInfo::exists(root + "/000100/000100.krc"));
        QVERIFY(!QFileInfo::exists(root + "/000100/000100.jpg"));
        QCOMPARE(read(source), QByteArray("external song to retain"));
        QCOMPARE(read(source + ".lrc"), QByteArray("external lyrics to retain"));
        QCOMPARE(read(outsideCover), QByteArray("external cover to retain"));
        QCOMPARE(read(root + "/000100/notes.txt"), QByteArray("unrelated file to retain"));
        QVERIFY(peer.call("player.status").value("data").toObject().value("queue").toArray().isEmpty());
        QVERIFY(peer.call("library.list").value("data").toObject().value("library").toObject().value("songs").toArray().isEmpty());
        runtime.stop();
        QVERIFY(runtime.start());
        Rpc restored;
        QVERIFY(restored.connect(qEnvironmentVariable("NEKOTUNE_SOCKET")));
        QTest::qWait(100);
        QVERIFY(restored.call("library.list").value("data").toObject().value("library").toObject().value("songs").toArray().isEmpty());
        QCOMPARE(restored.call("library.import", {{"path", source}}).value("status").toString(), QString("ok"));
        runtime.stop();
    }
    // The rejecting SQL trigger fails after filesystem staging has already occurred.
    // Both staged audio and lyrics must be restored when the database transaction fails.
    // Replacing a numbered directory with a symlink tests traversal beyond the owned tree.
    // Cleanup must reject that directory instead of trusting its apparently managed name.
    // The IPC flag is also validated by type; a string must not enable destructive cleanup.
    // Together these checks cover rollback and ownership rather than only successful deletion.
    void cleanupRollsBackOnDatabaseFailureAndRejectsDirectorySymlinks() {
        const auto source = m_profile.filePath("cleanup-rollback/song.mp3");
        write(source, "rollback original");
        BackendRuntime runtime;
        QVERIFY(runtime.start());
        Rpc peer;
        QVERIFY(peer.connect(qEnvironmentVariable("NEKOTUNE_SOCKET")));
        const auto imported = peer.call("library.import", {{"path", source}}).value("data").toObject();
        const int id = imported.value("song_id").toInt();
        const auto path = imported.value("path").toString();
        QVERIFY(id > 0);
        const auto folder = QFileInfo(path).absolutePath();
        const auto base = folder + '/' + QFileInfo(path).completeBaseName();
        write(base + ".lrc", "rollback lyrics");
        DatabaseSession db(qEnvironmentVariable("NEKOTUNE_DB_PATH"));
        QVERIFY(db.isReady());
        QSqlQuery query(db.database());
        QVERIFY(query.exec("CREATE TRIGGER reject_cleanup BEFORE DELETE ON songs BEGIN SELECT RAISE(ABORT, 'reject cleanup'); END"));
        auto result = peer.call("library.delete", {{"song_ids", QJsonArray{id}}, {"clean_files", true}});
        QCOMPARE(result.value("status").toString(), QString("error"));
        QVERIFY(QFileInfo(path).isSymLink());
        QCOMPARE(read(base + ".lrc"), QByteArray("rollback lyrics"));
        QCOMPARE(read(source), QByteArray("rollback original"));
        QVERIFY(query.exec("SELECT count(*) FROM managed_resources"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        QVERIFY(query.exec("DROP TRIGGER reject_cleanup"));
        const auto outside = m_profile.filePath("cleanup-rollback/relocated");
        QVERIFY(QDir().rename(folder, outside));
        QVERIFY(QFile::link(outside, folder));
        result = peer.call("library.delete", {{"song_ids", QJsonArray{id}}, {"clean_files", true}});
        QCOMPARE(result.value("status").toString(), QString("error"));
        QCOMPARE(read(base + ".lrc"), QByteArray("rollback lyrics"));
        QVERIFY(QFileInfo(path).isSymLink());
        QCOMPARE(read(source), QByteArray("rollback original"));
        QCOMPARE(peer.call("library.delete", {{"song_ids", QJsonArray{id}}, {"clean_files", "yes"}}).value("status").toString(), QString("error"));
        runtime.stop();
    }
    // Scanning discovers supported audio recursively without making a playback queue.
    // Partial downloads, configuration files and linked directories are outside the scan input.
    // A database-only deletion intentionally leaves bytes on disk but records scan suppression.
    // Restarting the store ensures that suppression is durable rather than an in-memory filter.
    // Manual import clears the relevant suppression and restores the known content identity.
    // Scan status must report the configured roots consistently with the discovered records.
    void scansAndKeepsDeletionAcrossRestart() {
        const auto root = AppPaths::musicDirectory();
        write(root + "/nested/song.mp3", "scanned song");
        write(root + "/config/ignored.mp3", "config song");
        write(root + "/pending.mp3.part", "unfinished");
        const auto outside = m_profile.filePath("outside");
        write(outside + "/outside.mp3", "outside");
        QVERIFY(QFile::link(outside, root + "/directory-link"));
        BackendRuntime runtime;
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        Rpc peer;
        QVERIFY(peer.connect(qEnvironmentVariable("NEKOTUNE_SOCKET")));
        auto songs = [&] {
            return peer.call("library.list")
                .value("data")
                .toObject()
                .value("library")
                .toObject()
                .value("songs")
                .toArray();
        };
        QTRY_COMPARE(songs().size(), 1);
        const auto song = songs().first().toObject();
        const auto id = song.value("song_id").toInt();
        const auto path = song.value("path").toString();
        QVERIFY(QFileInfo(path).isSymLink());
        QCOMPARE(song.value("title").toString(), QString("song"));
        QVERIFY(peer.call("player.status").value("data").toObject().value("queue").toArray().isEmpty());
        auto result = peer.call("library.delete", {{"song_ids", QJsonArray{id}}});
        QCOMPARE(result.value("status").toString(), QString("ok"));
        runtime.stop();
        QVERIFY(runtime.start());
        Rpc restored;
        QVERIFY(restored.connect(qEnvironmentVariable("NEKOTUNE_SOCKET")));
        QTest::qWait(150);
        QVERIFY(restored.call("library.list")
                    .value("data")
                    .toObject()
                    .value("library")
                    .toObject()
                    .value("songs")
                    .toArray()
                    .isEmpty());
        result = restored.call("library.import", {{"path", root + "/nested/song.mp3"}});
        QCOMPARE(result.value("status").toString(), QString("ok"));
        QCOMPARE(result.value("data").toObject().value("path").toString(), path);
        const auto status = restored.call("player.status").value("data").toObject();
        QCOMPARE(status.value("music_directory").toString(), root);
        QCOMPARE(status.value("config_directory").toString(), root + "/config");
        runtime.stop();
    }
    // Saving one preference must preserve settings owned by another feature.
    // An explicit environment language overrides a saved preference at startup.
    // The persisted configuration remains private to its owner after replacement.
    // This fixture checks both merged contents and the precedence used on restore.
    void settingsMergeAndLanguageRestores() {
        QVERIFY(AppPaths::saveSetting("lyrics_offline", true));
        I18n first;
        first.setLanguage(first.language() == "zh" ? "en" : "zh");
        I18n restored;
        QCOMPARE(restored.language(), first.language());
        QVERIFY(AppPaths::setting("lyrics_offline").toBool());
        qputenv("NEKOTUNE_LANGUAGE", "zh");
        I18n overridden;
        QCOMPARE(overridden.language(), QString("zh"));
        qunsetenv("NEKOTUNE_LANGUAGE");
        QCOMPARE(QFileInfo(AppPaths::configDirectory()).permissions() &
                     (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions{});
    }
    // The isolated home reproduces the old storage layout without touching user data.
    // Migration copies the legacy database rather than consuming or rewriting its original bytes.
    // Tags, playlists and queue records must survive along with the audio identities.
    // Lyrics assets move to the new profile layout independently of credential migration.
    // Without an installed account plugin, credential files stay available for later secure migration.
    // Plugin-specific secure migration is verified by the external plugin suite.
    void migratesLegacyDatabaseAndPreservesCredentials() {
        QTemporaryDir directory;
        const auto project = directory.filePath("project");
        const auto oldDb = project + "/build/nekotune.sqlite3";
        const auto home = directory.filePath("home");
        const auto config = home + "/Music/NekoTune/config";
        write(project + "/CMakeLists.txt", "# fixture");
        QDir().mkpath(project + "/backend");
        const auto audio = directory.filePath("old.wav");
        write(audio, "legacy audio");
        {
            StoreFixture old(oldDb);
            QVERIFY2(old.isReady(), qPrintable(old.errorString()));
            auto song = old.getOrCreateSong(hash("legacy audio"), audio, "Saved name", "Saved artist");
            QVERIFY(song);
            QVERIFY(old.updateMetadata(song->id, "Saved name", "Saved artist", "Saved lyrics",
                                       QStringList{"Old tag"}));
            const auto playlist = old.createPlaylist("Old playlist");
            QVERIFY(playlist > 0);
            QVERIFY(old.addPlaylistSong(playlist, {audio, song->id}));
            QVERIFY(old.saveQueue({{{audio, song->id}}, 0}));
        }
        const auto oldBytes = read(oldDb);
        const auto oldKey = directory.filePath("xdg-config/NekoTune/kugou-account-key");
        const auto oldSession = directory.filePath("xdg-data/NekoTune/kugou-session.json");
        write(oldKey, "fixture-key");
        write(oldSession, "{\"version\":1,\"cookies\":{}}");
        write(directory.filePath("xdg-data/NekoTune/NekoTune/lyrics-cache/sample.json"), "{}");
        write(directory.filePath("xdg-cache/NekoTune/NekoTune/covers/sample"), "cached image");
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.remove("NEKOTUNE_HOME");
        environment.remove("NEKOTUNE_DB_PATH");
        environment.insert("HOME", home);
        environment.insert("XDG_CONFIG_HOME", directory.filePath("xdg-config"));
        environment.insert("XDG_DATA_HOME", directory.filePath("xdg-data"));
        environment.insert("XDG_CACHE_HOME", directory.filePath("xdg-cache"));
        const auto socket = directory.filePath("migration.sock");
        environment.insert("NEKOTUNE_SOCKET", socket);
        QProcess backend;
        backend.setProcessEnvironment(environment);
        backend.setWorkingDirectory(project);
        backend.start(NEKOTUNE_BACKEND_BINARY);
        QVERIFY(backend.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(socket).exists(), 5000);
        Rpc peer;
        QVERIFY(peer.connect(socket));
        const auto status = peer.call("player.status").value("data").toObject();
        QCOMPARE(status.value("database_path").toString(), config + "/nekotune.sqlite3");
        QCOMPARE(status.value("queue").toArray().size(), 1);
        QCOMPARE(status.value("playlists").toArray().first().toObject().value("name").toString(),
                 QString("Old playlist"));
        // Without an installed plugin, migration leaves credentials available for a later opt-in.
        QCOMPARE(read(oldKey), QByteArray("fixture-key"));
        QCOMPARE(read(oldSession), QByteArray("{\"version\":1,\"cookies\":{}}"));
        QVERIFY(QFileInfo(config + "/lyrics-cache/sample.json").isFile());
        QVERIFY(QFileInfo(config + "/covers/sample").isFile());
        backend.terminate();
        QVERIFY(backend.waitForFinished());
        QCOMPARE(read(oldDb), oldBytes);
    }
};
QTEST_GUILESS_MAIN(MusicDirectoryTest)
#include "music_directory_test.moc"
