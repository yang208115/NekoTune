#include "app_paths.h"
#include "application/cover_service.h"
#include "i18n.h"
#include "infrastructure/lyrics_storage.h"
#include "infrastructure/music_directory.h"
#include "infrastructure/sidecar_store.h"
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
    void migratesLegacyDatabaseAndSecuresCredentials() {
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
        QVERIFY(!QFileInfo::exists(config + "/kugou-account-key"));
        QVERIFY(!QFileInfo::exists(config + "/kugou-session.json"));
        QVERIFY(peer.call("kugou.status").value("data").toObject().value("kugou").toObject().value("key_saved").toBool());
        QVERIFY(QFileInfo(config + "/lyrics-cache/sample.json").isFile());
        QVERIFY(QFileInfo(config + "/covers/sample").isFile());
        backend.terminate();
        QVERIFY(backend.waitForFinished());
        QCOMPARE(read(oldDb), oldBytes);
        QVERIFY(!QFileInfo::exists(oldKey));
        QVERIFY(!QFileInfo::exists(oldSession));
        // The keyring is authoritative over stale plaintext copies; clearing stays cleared.
        write(config + "/kugou-account-key", "new-key");
        backend.start(NEKOTUNE_BACKEND_BINARY);
        QVERIFY(backend.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(socket).exists(), 5000);
        Rpc restarted;
        QTRY_VERIFY(restarted.connect(socket));
        QVERIFY(!QFileInfo::exists(config + "/kugou-account-key"));
        QCOMPARE(restarted.call("kugou.clear_key").value("status").toString(), QString("ok"));
        backend.terminate();
        QVERIFY(backend.waitForFinished());
        backend.start(NEKOTUNE_BACKEND_BINARY);
        QVERIFY(backend.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(socket).exists(), 5000);
        Rpc cleared;
        QTRY_VERIFY(cleared.connect(socket));
        QVERIFY(!QFileInfo(config + "/kugou-account-key").exists());
        QVERIFY(!cleared.call("kugou.status").value("data").toObject().value("kugou").toObject().value("key_saved").toBool());
        backend.terminate();
        QVERIFY(backend.waitForFinished());
    }
};
QTEST_GUILESS_MAIN(MusicDirectoryTest)
#include "music_directory_test.moc"
