#include "app_paths.h"
#include "application/lyrics/lyrics_service.h"
#include "application/playback/player_engine.h"
#include "controllers/app_controllers.h"
#include "i18n.h"
#include "infrastructure/library/import_executor.h"
#include "infrastructure/library/library_file_io.h"
#include "infrastructure/lyrics/lyrics_storage.h"
#include "ipc/ipc_router.h"
#include "ipc/serialization/serialization.h"
#include "runtime/backend_runtime.h"
#include "support/audio_backend.h"
#include "support/store_fixture.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QJSValue>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace nekotune;

static QQuickItem *visualItem(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems())
        if (auto *found = visualItem(child, name))
            return found;
    return nullptr;
}

class FakeAudio final : public TestAudioBackend {
  public:
    QUrl path;
    bool playing = false;
    double level = .8;
    qint64 clock = 0;
    void setSource(const QUrl &url) override { path = url; }
    QUrl source() const override { return path; }
    void play() override {
        playing = true;
        emit stateChanged(PlayerState::Playing);
    }
    void pause() override {
        playing = false;
        emit stateChanged(PlayerState::Paused);
    }
    void stop() override {
        playing = false;
        emit stateChanged(PlayerState::Stopped);
    }
    void seek(qint64 value) override {
        clock = value;
        emit positionChanged(value);
    }
    void setVolume(double value) override { level = value; }
    qint64 position() const override { return clock; }
    qint64 duration() const override { return path.isEmpty() ? 0 : 30000; }
    double volume() const override { return level; }
    AudioMetadata metadata() const override { return {}; }
};

static std::unique_ptr<IPlaybackBackend> makeFakeAudio() {
    return std::make_unique<FakeAudio>();
}

class ExtraProvider final : public LyricsProvider {
  public:
    LyricsSource descriptor() const override { return {"extra", "Extra provider", true, false}; }
    void cancel() override {}
    void request(const LyricsQuery &query, quint64 token, bool) override {
        LyricsCandidate candidate;
        candidate.document.source = "extra";
        candidate.document.plainLyrics = "Registered provider works";
        candidate.document.matched = query;
        emit completed(token, {candidate});
    }
};

class RpcPeer {
  public:
    QLocalSocket socket;
    QByteArray buffer;
    int nextId = 1;
    bool connect(const QString &path) {
        socket.connectToServer(path);
        return socket.waitForConnected(3000);
    }
    QJsonObject call(const QString &method, const QJsonObject &params = {}) {
        const int id = nextId++;
        socket.write(QJsonDocument(QJsonObject{{"id", id}, {"method", method}, {"params", params}})
                         .toJson(QJsonDocument::Compact) +
                     "\n");
        socket.flush();
        QElapsedTimer timeout;
        timeout.start();
        while (timeout.elapsed() < 5000) {
            buffer += socket.readAll();
            while (buffer.contains('\n')) {
                auto end = buffer.indexOf('\n');
                auto result = QJsonDocument::fromJson(buffer.left(end)).object();
                buffer.remove(0, end + 1);
                if (result.value("id").toInt() == id)
                    return result;
            }
            socket.waitForReadyRead(50);
        }
        return {{"status", "timeout"}, {"method", method}};
    }
};

class RefactorTest final : public QObject {
    Q_OBJECT
  private:
    QTemporaryDir m_directory;
    QString m_audio;
    QString m_socket;
    QString m_database;
    void writeAudio(const QString &path, char sample, int seconds = 30) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        const quint32 size = 8000 * 2 * seconds;
        file.write("RIFF");
        stream << quint32(size + 36);
        file.write("WAVEfmt ");
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2)
               << quint16(16);
        file.write("data");
        stream << size;
        file.write(QByteArray(size, sample));
    }
  private slots:
    void initTestCase() {
        QVERIFY(m_directory.isValid());
        m_socket = m_directory.filePath("backend.sock");
        m_database = m_directory.filePath("player.sqlite3");
        m_audio = m_directory.filePath("test.wav");
        qputenv("NEKOTUNE_SOCKET", m_socket.toUtf8());
        qputenv("NEKOTUNE_DB_PATH", m_database.toUtf8());
        qputenv("NEKOTUNE_HOME", m_directory.filePath("music").toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath("config").toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath("data").toUtf8());
        qputenv("XDG_CACHE_HOME", m_directory.filePath("cache").toUtf8());
        qunsetenv("KUGOU_ACCOUNT_API_KEY");
        qunsetenv("KUGOU_ACCOUNT_API_KEY_FILE");
        writeAudio(m_audio, 0);
    }
    // Inject queue persistence failure while a fake decoder is actively playing.
    // Clear/remove must preserve the live source and durable queue on failure.
    // Successful final removal must release the source so Play cannot revive it.
    // Volume zero must survive as mute rather than falling back to a default.
    // A title-only metadata patch must preserve previously stored full lyrics.
    // This combines rollback and sparse-update invariants across service boundaries.
    void playbackAndStorageFailuresAreAtomic() {
        StoreFixture store(m_directory.filePath("transactions.sqlite3"));
        auto song = store.getOrCreateSong("test", m_audio);
        QVERIFY(song);
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        FakeAudio audio;
        PlayerEngine player(audio, queue);
        auto candidate = queue.queue();
        auto id = candidate.add(m_audio, *song);
        candidate.setCurrentIndex(0);
        QVERIFY(player.replaceQueue(candidate));
        QVERIFY(audio.playing);
        QSqlQuery sql(store.db.database());
        QVERIFY(sql.exec("CREATE TRIGGER fail_queue BEFORE DELETE ON queue_items BEGIN SELECT RAISE(FAIL, "
                         "'injected failure'); END"));
        QVERIFY(!player.clear());
        QVERIFY(!player.removeItem(id));
        QCOMPARE(queue.queue().size(), 1);
        QVERIFY(audio.playing);
        QVERIFY(!audio.source().isEmpty());
        QCOMPARE(store.loadQueue().items.size(), 1);
        QVERIFY(sql.exec("DROP TRIGGER fail_queue"));
        QVERIFY(player.removeItem(id));
        QVERIFY(audio.source().isEmpty());
        QVERIFY(!player.play());
        QCOMPARE(queue.queue().size(), 0);
        QVERIFY(player.setVolume(0));
        QCOMPARE(player.snapshot().volume, 0.0);
        auto update = store.library.update(song->id, {{}, {}, QString("saved lyrics"), {}});
        QVERIFY(update);
        MetadataPatch patch;
        patch.title = "Renamed";
        QVERIFY(store.library.update(song->id, patch));
        QCOMPARE(store.songById(song->id)->lyrics, QString("saved lyrics"));
    }
    // A serialized slow route occupies the mutation queue until its completion.
    // An immediate query must run while the later mutation is still waiting.
    // The request ID is correlated by the router rather than individual handlers.
    // Duplicate completion must not run the waiting command twice.
    // Duplicate method registration is rejected without replacing the existing route.
    // The test proves extension routing does not require a growing central switch.
    void registeredMethodsAndAsyncOrdering() {
        IpcRouter router;
        IpcRouter::Completion pending;
        QStringList order;
        QVERIFY(router.registerMethod(
            "extra.slow",
            [&](const QJsonObject &, auto done) {
                order << "started";
                pending = done;
            },
            true));
        QVERIFY(router.registerMethod(
            "extra.mutate",
            [&](const QJsonObject &, auto done) {
                order << "mutated";
                done(success());
            },
            true));
        QVERIFY(router.registerMethod("extra.status", [&](const QJsonObject &, auto done) {
            order << "queried";
            done(success());
        }));
        QVERIFY(!router.registerMethod("extra.status", {}));
        QJsonObject first;
        router.dispatch({{"id", "first"}, {"method", "extra.slow"}}, [&](auto result) { first = result; });
        router.dispatch({{"id", 2}, {"method", "extra.mutate"}}, [](auto) {});
        router.dispatch({{"id", 3}, {"method", "extra.status"}}, [](auto) {});
        QCOMPARE(order, QStringList({"started", "queried"}));
        pending(success());
        QTRY_COMPARE(order.size(), 3);
        QCOMPARE(order.last(), QString("mutated"));
        QCOMPARE(first.value("id").toString(), QString("first"));
        pending(success());
        QCOMPARE(order.size(), 3);
        router.shutdown();
    }
    // Register an extra provider through the existing typed extension seam.
    // It must appear in source descriptors and receive explicit-source search.
    // Candidate selection resolves through the same service state machine.
    // No provider-specific logic should be needed in PlayerEngine or IPC dispatch.
    // This guards the intended extension point with a minimal injected provider.
    void registeredLyricsSource() {
        ExtraProvider provider;
        LyricsService service({&provider}, std::make_unique<LyricsStorage>(m_directory.filePath("lyrics")));
        QCOMPARE(service.sources().first().id, QString("extra"));
        LyricsSnapshot result;
        connect(&service, &LyricsService::changed, this, [&](const auto &value) { result = value; });
        service.search({"Song", "Artist", {}, 30000, "id"}, 1, "extra");
        QCOMPARE(result.state, QString("candidates"));
        service.select(0, 1);
        QCOMPARE(result.state, QString("ready"));
        QCOMPARE(result.document->source, QString("extra"));
    }
    // Exercise public methods against the composed backend over a real local socket.
    // Removing the final item makes later Play fail rather than reviving stale media.
    // Source enumeration remains compatible with registered default providers.
    // Stop the runtime while a client is still connected to exercise buffer ownership.
    // Starting again must provide a usable new socket session without old callbacks.
    void playbackStatisticsStayConsistentOverIpcAndRestart() {
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        QCOMPARE(peer.call("lyrics.set_offline", {{"offline", true}}).value("status").toString(), "ok");
        QCOMPARE(peer.call("queue.clear").value("status").toString(), "ok");
        QCOMPARE(peer.call("player.play", {{"path", m_audio}}).value("status").toString(), "ok");
        const auto current = peer.call("player.status").value("data").toObject().value("song").toObject();
        const int id = current.value("song_id").toInt();
        QVERIFY(id > 0);
        const qint64 count = current.value("play_count").toInteger();
        QVERIFY(count > 0);
        QCOMPARE(current.value("import_source").toString(), "local");
        auto metadata = [&] {
            return peer.call("song.metadata", {{"song_id", id}}).value("data").toObject();
        };
        QCOMPARE(metadata().value("play_count").toInteger(), count);
        QCOMPARE(peer.call("player.pause").value("status").toString(), "ok");
        QCOMPARE(peer.call("player.play").value("status").toString(), "ok");
        QCOMPARE(metadata().value("play_count").toInteger(), count);
        QCOMPARE(peer.call("player.stop").value("status").toString(), "ok");
        QCOMPARE(peer.call("player.play").value("status").toString(), "ok");
        QCOMPARE(metadata().value("play_count").toInteger(), count + 1);
        const auto items = peer.call("queue.status").value("data").toObject().value("items").toArray();
        QCOMPARE(items.first().toObject().value("play_count").toInteger(), count + 1);
        const auto library = peer.call("library.list").value("data").toObject().value("library").toObject();
        bool found = false;
        for (const auto &song : library.value("songs").toArray()) {
            if (song.toObject().value("song_id").toInt() == id) {
                QCOMPARE(song.toObject().value("play_count").toInteger(), count + 1);
                found = true;
            }
        }
        QVERIFY(found);
        runtime.stop();
        QVERIFY(runtime.start());
        RpcPeer restored;
        QVERIFY(restored.connect(m_socket));
        QCOMPARE(restored.call("song.metadata", {{"song_id", id}}).value("data").toObject()
                     .value("play_count").toInteger(), count + 1);
        QCOMPARE(restored.call("player.play").value("status").toString(), "ok");
        QCOMPARE(restored.call("player.status").value("data").toObject().value("song").toObject()
                     .value("play_count").toInteger(), count + 2);
        runtime.stop();
    }
    void storedDurationReachesFrontendOnTrackChanges() {
        const auto shortAudio = m_directory.filePath("short-duration.wav");
        writeAudio(shortAudio, 1, 2);
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        QCOMPARE(peer.call("lyrics.set_offline", {{"offline", true}}).value("status").toString(), "ok");
        QCOMPARE(peer.call("queue.clear").value("status").toString(), "ok");
        IpcClient client;
        PlaybackController playback(client);
        QTRY_VERIFY(client.connected());
        QTRY_COMPARE(playback.duration(), 0);
        // This peer's responses never reach the frontend controller; only broadcast events do.
        QCOMPARE(peer.call("player.play", {{"path", m_audio}}).value("status").toString(), "ok");
        QTRY_COMPARE(playback.duration(), 30000);
        QCOMPARE(peer.call("player.play", {{"path", shortAudio}}).value("status").toString(), "ok");
        QTRY_COMPARE(playback.duration(), 2000);
        const auto status = peer.call("player.status").value("data").toObject();
        QCOMPARE(status.value("duration").toInteger(), 2000);
        QCOMPARE(status.value("song").toObject().value("duration_ms").toInteger(), 2000);
        runtime.stop();
        QVERIFY(runtime.start());
        RpcPeer restored;
        QVERIFY(restored.connect(m_socket));
        QCOMPARE(restored.call("player.status").value("data").toObject().value("duration").toInteger(), 2000);
        QTRY_VERIFY(client.connected());
        QTRY_COMPARE(playback.duration(), 2000);
        QCOMPARE(restored.call("queue.clear").value("status").toString(), "ok");
        QTRY_COMPARE(playback.duration(), 0);
        runtime.stop();
    }
    void socketCompatibilityAndConnectedShutdown() {
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        QCOMPARE(peer.call("lyrics.set_offline", {{"offline", true}}).value("status").toString(),
                 QString("ok"));
        QCOMPARE(peer.call("queue.clear").value("status").toString(), QString("ok"));
        auto played = peer.call("player.play", {{"path", m_audio}});
        QCOMPARE(played.value("status").toString(), QString("ok"));
        auto state = peer.call("player.status").value("data").toObject();
        int id = state.value("song").toObject().value("queue_id").toInt();
        QCOMPARE(peer.call("queue.remove", {{"id", id}}).value("status").toString(), QString("ok"));
        QCOMPARE(peer.call("player.play").value("status").toString(), QString("error"));
        auto sources = peer.call("lyrics.sources").value("data").toObject().value("sources").toArray();
        QCOMPARE(sources.size(), 1);
        QCOMPARE(sources.first().toObject().value("id").toString(), QString("lrclib"));
        runtime.stop(); // Live socket must not call into a destroyed client-buffer map.
        QVERIFY(runtime.start());
        RpcPeer reconnected;
        QVERIFY(reconnected.connect(m_socket));
        QVERIFY(peer.socket.waitForDisconnected(50) || peer.socket.state() == QLocalSocket::UnconnectedState);
        QCOMPARE(reconnected.call("player.status").value("status").toString(), QString("ok"));
        runtime.stop();
    }
    void unavailableOutputPreservesQueueAcrossRuntimeRestart() {
        int creations = 0;
        QThread *audioThread = nullptr;
        BackendRuntime runtime([&] {
            ++creations;
            auto audio = std::make_unique<FakeAudio>();
            audioThread = audio->thread();
            audio->setDevices({});
            return audio;
        });
        QVERIFY(runtime.start());
        QCOMPARE(creations, 1);
        QVERIFY(audioThread != QThread::currentThread());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        QCOMPARE(peer.call("lyrics.set_offline", {{"offline", true}}).value("status").toString(),
                 QString("ok"));
        QCOMPARE(peer.call("queue.clear").value("status").toString(), QString("ok"));
        const auto played = peer.call("player.play", {{"path", m_audio}});
        QCOMPARE(played.value("status").toString(), QString("ok"));
        const auto state = played.value("data").toObject();
        QCOMPARE(state.value("state").toString(), QString("paused"));
        const auto output = state.value("audio_output").toObject();
        QVERIFY(!output.value("available").toBool());
        QVERIFY(output.contains("active_id"));
        QVERIFY(output.value("active_id").toString().isEmpty());
        const auto queue = state.value("queue").toArray();
        QCOMPARE(queue.size(), 1);
        QCOMPARE(peer.call("player.play").value("status").toString(), QString("error"));
        QCOMPARE(peer.call("player.status").value("data").toObject().value("queue").toArray(), queue);
        runtime.stop();

        QVERIFY(runtime.start());
        QCOMPARE(creations, 2);
        QVERIFY(audioThread != QThread::currentThread());
        RpcPeer restored;
        QVERIFY(restored.connect(m_socket));
        const auto reopened = restored.call("player.status").value("data").toObject();
        QCOMPARE(reopened.value("queue").toArray(), queue);
        QVERIFY(reopened.value("state").toString() != "playing");
        QVERIFY(!reopened.value("audio_output").toObject().value("available").toBool());
        QCOMPARE(restored.call("queue.clear").value("status").toString(), QString("ok"));
        runtime.stop();
    }
    // Shutdown can happen before accepted file inspections finish normally.
    // Each accepted callback must settle as Cancelled exactly once.
    // Processing queued events afterward must not deliver an old successful result.
    // Repeated shutdown is safe and new requests are immediately cancelled.
    // This protects scheduler release and dialog busy state during application exit.
    void importShutdownCompletesAcceptedTasksOnce() {
        ImportExecutor executor;
        int completed = 0;
        auto cancelled = [&](Result<ImportedFile> result) {
            QVERIFY(!result);
            QCOMPARE(result.error().code, ErrorCode::Cancelled);
            ++completed;
        };
        executor.inspect(m_audio, cancelled);
        executor.inspect(m_audio, cancelled);
        executor.shutdown();
        QCOMPARE(completed, 2);
        QCoreApplication::processEvents();
        QCOMPARE(completed, 2);
        executor.shutdown();
        executor.inspect(m_audio, cancelled);
        QCOMPARE(completed, 3);
    }
    // Send several appends before waiting for their inspection callbacks.
    // Serialized commands must preserve request order, including repeated audio entries.
    // An immediate status query remains usable while imports are pending.
    // Disconnecting the sender does not cancel already admitted mutations.
    // A second client observes their eventual committed queue without resending them.
    // Runtime shutdown then exercises cancellation of a deliberately large pending import.
    void orderedImportsAndDisconnect() {
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        peer.call("lyrics.set_offline", {{"offline", true}});
        peer.call("queue.clear");
        const auto second = m_directory.filePath("second.wav");
        writeAudio(second, 1);
        const QStringList paths{m_audio, second, m_audio};
        for (int index = 0; index < paths.size(); ++index) {
            peer.socket.write(QJsonDocument(QJsonObject{{"id", 100 + index},
                                                        {"method", "queue.add"},
                                                        {"params", QJsonObject{{"path", paths[index]}}}})
                                  .toJson(QJsonDocument::Compact) +
                              "\n");
        }
        peer.socket.flush();
        // Reads remain available while queued imports are being completed.
        QCOMPARE(peer.call("player.status").value("status").toString(), QString("ok"));
        peer.socket.disconnectFromServer();
        RpcPeer observer;
        QVERIFY(observer.connect(m_socket));
        QJsonArray queue;
        QTRY_VERIFY_WITH_TIMEOUT(
            ([&] {
                queue = observer.call("player.status").value("data").toObject().value("queue").toArray();
                return queue.size() == 3;
            })(),
            5000);
        for (int index = 0; index < paths.size(); ++index)
            QCOMPARE(LibraryFileIO::hashFile(queue[index].toObject().value("path").toString()).value(),
                     LibraryFileIO::hashFile(paths[index]).value());
        QCOMPARE(queue[0].toObject().value("song_id"), queue[2].toObject().value("song_id"));
        QVERIFY(queue[0].toObject().value("queue_id") != queue[2].toObject().value("queue_id"));
        const auto large = m_directory.filePath("pending.bin");
        QFile file(large);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(64 * 1024 * 1024));
        file.close();
        observer.socket.write(QJsonDocument(QJsonObject{{"id", 200},
                                                        {"method", "library.import"},
                                                        {"params", QJsonObject{{"path", large}}}})
                                  .toJson(QJsonDocument::Compact) +
                              "\n");
        observer.socket.flush();
        QCOMPARE(observer.call("player.pause").value("status").toString(), QString("ok"));
        QTest::qWait(5);
        runtime.stop();
    }
    // Explicit song_ids defines order and may be a subset of the collection.
    // Invalid/duplicate/empty identities must not replace the audible queue.
    // An injected queue-save failure must preserve song and playing state too.
    // Calls omitting song_ids retain the complete collection's old API behavior.
    // Playlist playback rejects unavailable members; library playback reports skipped files.
    // Import-only remains independent even when a visible sequence is playing.
    // Reopening verifies the accepted survivor sequence was durably persisted.
    void orderedVisiblePlaybackPreservesQueueOnFailure() {
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        peer.call("lyrics.set_offline", {{"offline", true}});
        QVector<int> ids;
        for (int index = 0; index < 3; ++index) {
            const auto path = m_directory.filePath(QString("visible-%1.wav").arg(index));
            writeAudio(path, char(100 + index));
            auto result = peer.call("library.import", {{"path", path}});
            QCOMPARE(result.value("status").toString(), QString("ok"));
            ids.append(result.value("data").toObject().value("song_id").toInt());
            QCOMPARE(peer.call("song.update_metadata", {{"song_id", ids.last()}, {"tags", QJsonArray{"Visible"}}})
                         .value("status").toString(), QString("ok"));
        }
        const auto library = peer.call("library.list").value("data").toObject().value("library").toObject();
        int tagId = 0;
        for (const auto &tag : library.value("tags").toArray())
            if (tag.toObject().value("name").toString() == "Visible") tagId = tag.toObject().value("id").toInt();
        QVERIFY(tagId > 0);
        const auto playlist = peer.call("playlist.create", {{"name", "Visible result"}})
                                  .value("data").toObject().value("playlist_id").toInt();
        for (int id : ids)
            QCOMPARE(peer.call("playlist.add", {{"id", playlist}, {"song_id", id}}).value("status").toString(), QString("ok"));
        const QJsonArray visible{ids[2], ids[0]};
        auto result = peer.call("library.play", {{"tag_ids", QJsonArray{tagId}}, {"song_ids", visible}, {"song_id", ids[0]}});
        QCOMPARE(result.value("status").toString(), QString("ok"));
        auto snapshot = result.value("data").toObject();
        auto queue = snapshot.value("queue").toArray();
        QCOMPARE(queue.size(), 2);
        QCOMPARE(queue[0].toObject().value("song_id").toInt(), ids[2]);
        QCOMPARE(queue[1].toObject().value("song_id").toInt(), ids[0]);
        QCOMPARE(queue[1].toObject().value("state").toString(), QString("current"));
        for (const auto &invalid : {QJsonArray{}, QJsonArray{ids[0], ids[0]}, QJsonArray{999999}, QJsonArray{"bad"}}) {
            auto failed = peer.call("library.play", {{"song_ids", invalid}});
            QCOMPARE(failed.value("status").toString(), QString("error"));
            QCOMPARE(peer.call("player.status").value("data").toObject().value("queue").toArray(), queue);
        }
        auto outside = peer.call("library.play", {{"song_ids", visible}, {"song_id", ids[1]}});
        QCOMPARE(outside.value("status").toString(), QString("error"));
        QCOMPARE(peer.call("player.status").value("data").toObject().value("queue").toArray(), queue);
        result = peer.call("playlist.play", {{"id", playlist}, {"song_ids", visible}, {"song_id", ids[2]}});
        QCOMPARE(result.value("status").toString(), QString("ok"));
        queue = result.value("data").toObject().value("queue").toArray();
        QCOMPARE(queue.size(), 2);
        QCOMPARE(queue[0].toObject().value("song_id").toInt(), ids[2]);
        QCOMPARE(queue[0].toObject().value("state").toString(), QString("current"));
        QCOMPARE(peer.call("playlist.play", {{"id", playlist}, {"song_ids", QJsonArray{999999}}}).value("status").toString(), QString("error"));
        QCOMPARE(peer.call("player.status").value("data").toObject().value("queue").toArray(), queue);
        {
            auto database = QSqlDatabase::addDatabase("QSQLITE", "visible-failure");
            database.setDatabaseName(m_database);
            QVERIFY(database.open());
            QSqlQuery sql(database);
            QVERIFY(sql.exec("CREATE TRIGGER fail_visible_queue BEFORE DELETE ON queue_items BEGIN SELECT RAISE(FAIL, 'visible save failed'); END"));
            // Queue rollback must preserve the injected backend's playing state.
            QTRY_COMPARE(peer.call("player.status").value("data").toObject().value("state").toString(),
                         QString("playing"));
            const auto before = peer.call("player.status").value("data").toObject();
            QCOMPARE(peer.call("library.play", {{"song_ids", QJsonArray{ids[1]}}}).value("status").toString(), QString("error"));
            const auto after = peer.call("player.status").value("data").toObject();
            QCOMPARE(after.value("queue"), before.value("queue"));
            QCOMPARE(after.value("song"), before.value("song"));
            QCOMPARE(after.value("state"), before.value("state"));
            QVERIFY(sql.exec("DROP TRIGGER fail_visible_queue"));
            database.close();
        }
        QSqlDatabase::removeDatabase("visible-failure");
        // Calls without the new field retain the full collection.
        result = peer.call("library.play", {{"tag_ids", QJsonArray{tagId}}});
        QCOMPARE(result.value("status").toString(), QString("ok"));
        QCOMPARE(result.value("data").toObject().value("queue").toArray().size(), 3);
        result = peer.call("playlist.play", {{"id", playlist}, {"song_id", ids[2]}});
        QCOMPARE(result.value("status").toString(), QString("ok"));
        QCOMPARE(result.value("data").toObject().value("queue").toArray().size(), 3);
        // An import, even with an active track, must not replace or append to the queue.
        const auto beforeImport = peer.call("player.status").value("data").toObject().value("queue");
        auto importPath = m_directory.filePath("import-only.wav");
        writeAudio(importPath, char(120));
        QCOMPARE(peer.call("library.import", {{"path", importPath}}).value("status").toString(), QString("ok"));
        QCOMPARE(peer.call("player.status").value("data").toObject().value("queue"), beforeImport);
        QVERIFY(QFile::remove(
            peer.call("song.metadata", {{"song_id", ids[0]}})["data"].toObject()["first_path"].toString()));
        QCOMPARE(peer.call("playlist.play", {{"id", playlist}, {"song_ids", visible}}).value("status").toString(), QString("error"));
        QCOMPARE(peer.call("player.status").value("data").toObject().value("queue"), beforeImport);
        result = peer.call("library.play", {{"tag_ids", QJsonArray{tagId}}, {"song_ids", visible}, {"song_id", ids[2]}});
        QCOMPARE(result.value("status").toString(), QString("ok"));
        QCOMPARE(result.value("data").toObject().value("skipped_song_ids").toArray(), QJsonArray{ids[0]});
        QCOMPARE(result.value("data").toObject().value("queue").toArray().size(), 1);
        const auto retained = peer.call("player.status").value("data").toObject().value("queue");
        QCOMPARE(peer.call("library.play", {{"song_ids", visible}, {"song_id", ids[0]}}).value("status").toString(), QString("error"));
        QCOMPARE(peer.call("player.status").value("data").toObject().value("queue"), retained);
        runtime.stop();
        BackendRuntime restored(makeFakeAudio);
        QVERIFY(restored.start());
        RpcPeer reopened;
        QVERIFY(reopened.connect(m_socket));
        const auto restoredQueue = reopened.call("player.status").value("data").toObject().value("queue").toArray();
        QCOMPARE(restoredQueue.size(), 1);
        QCOMPARE(restoredQueue[0].toObject().value("song_id").toInt(), ids[2]);
        QCOMPARE(restoredQueue[0].toObject().value("state").toString(), QString("current"));
        restored.stop();
    }
    // One audio identity is shown through library, queue, playlist and current playback.
    // All routes must expose the same backend-resolved cover URL.
    // Cached artwork changes and local sidecars exercise resolver invalidation.
    // Offline mode must prevent remote cover use across every view, not just the player.
    // The test catches missing enrichment on otherwise correct serialization paths.
    void artworkMatchesAcrossIpcCollections() {
        QVERIFY(AppPaths::saveSetting("lyrics_offline", false));
        const auto path = m_directory.filePath("covers.wav");
        writeAudio(path, char(83));
        QFile audio(path);
        QVERIFY(audio.open(QIODevice::ReadOnly));
        LyricsQuery query;
        query.trackId = QString::fromLatin1(
            QCryptographicHash::hash(audio.readAll(), QCryptographicHash::Sha256).toHex());
        audio.close();
        LyricsDocument document;
        document.source = "kugou";
        document.plainLyrics = "Cached lyrics prevent network requests";
        document.coverUrl = "https://imge.kugou.com/cached-cover.jpg";
        QVERIFY(LyricsCache().write(query, document));

        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        peer.call("queue.clear");
        const auto imported = peer.call("library.import", {{"path", path}});
        QCOMPARE(imported.value("status").toString(), QString("ok"));
        const int songId = imported.value("data").toObject().value("song_id").toInt();
        const auto playlist = peer.call("playlist.create", {{"name", "Artwork"}})
                                  .value("data")
                                  .toObject()
                                  .value("playlist_id")
                                  .toInt();
        QVERIFY(playlist > 0);
        QCOMPARE(
            peer.call("playlist.add", {{"id", playlist}, {"song_id", songId}}).value("status").toString(),
            QString("ok"));
        auto libraryCover = [&] {
            const auto songs = peer.call("library.list")
                                   .value("data")
                                   .toObject()
                                   .value("library")
                                   .toObject()
                                   .value("songs")
                                   .toArray();
            for (const auto &song : songs)
                if (song.toObject().value("song_id").toInt() == songId)
                    return song.toObject().value("cover_url").toString();
            return QString("missing song");
        };
        QCOMPARE(libraryCover(),
                 document.coverUrl); // Artwork is available before first playback.
        QCOMPARE(peer.call("player.play", {{"path", path}}).value("status").toString(), QString("ok"));
        IpcClient client;
        AppControllers controllers(client);
        QTRY_VERIFY(client.connected());
        auto modelCover = [&] {
            for (const auto &song : controllers.library->songs()->items())
                if (song.toMap().value("song_id").toInt() == songId)
                    return song.toMap().value("cover_url").toString();
            return QString("missing song");
        };
        QTRY_COMPARE(modelCover(), document.coverUrl);
        QTRY_COMPARE(controllers.playback->song().value("cover_url").toString(), document.coverUrl);
        auto verifyCovers = [&](const QString &expected) {
            QCOMPARE(libraryCover(), expected);
            const auto status = peer.call("player.status").value("data").toObject();
            QCOMPARE(status.value("song").toObject().value("cover_url").toString(), expected);
            QCOMPARE(status.value("queue").toArray().first().toObject().value("cover_url").toString(),
                     expected);
            const auto queue = peer.call("queue.status").value("data").toObject().value("items").toArray();
            QCOMPARE(queue.first().toObject().value("cover_url").toString(), expected);
            for (const auto &list : status.value("playlists").toArray())
                if (list.toObject().value("id").toInt() == playlist)
                    QCOMPARE(list.toObject()
                                 .value("items")
                                 .toArray()
                                 .first()
                                 .toObject()
                                 .value("cover_url")
                                 .toString(),
                             expected);
        };
        verifyCovers(document.coverUrl);
        peer.call("lyrics.set_offline", {{"offline", true}});
        QTRY_COMPARE(libraryCover(), QString());
        QTRY_COMPARE(modelCover(), QString());
        QTRY_COMPARE(controllers.playback->song().value("cover_url").toString(), QString());
        verifyCovers({});
        QImage image(40, 40, QImage::Format_RGB32);
        image.fill(QColor("#E8A9C3"));
        const auto coverPath = m_directory.filePath("covers.png");
        QVERIFY(image.save(coverPath));
        // Reimport collects a new external sidecar into the self-contained
        // library.
        QCOMPARE(peer.call("library.import", {{"path", path}})["status"].toString(), QString("ok"));
        const auto ownedCover = libraryCover();
        QVERIFY(QUrl(ownedCover).toLocalFile().contains("/artwork/"));
        verifyCovers(ownedCover);
        QVERIFY(QFile::remove(coverPath));
        verifyCovers(ownedCover);
        peer.call("lyrics.set_offline", {{"offline", false}});
        QTRY_COMPARE(modelCover(), ownedCover);
        QTRY_COMPARE(controllers.playback->song().value("cover_url").toString(), ownedCover);
        runtime.stop();

        QVERIFY(runtime.start());
        RpcPeer restored;
        QVERIFY(restored.connect(m_socket));
        const auto status = restored.call("player.status").value("data").toObject();
        QCOMPARE(status.value("song").toObject().value("cover_url").toString(), ownedCover);
        runtime.stop();
    }
    void embeddedImportAlbumAndBatchEditingOverIpc() {
        const auto tagged = m_directory.filePath("embedded.flac");
        QVERIFY(QFile::copy(QStringLiteral(NEKOTUNE_SOURCE_DIR "/tests/fixtures/tagged.flac"), tagged));
        const auto second = m_directory.filePath("album-second.wav");
        writeAudio(second, char(119), 32);
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        peer.call("lyrics.set_offline", {{"offline", true}});
        QVariantList ids;
        for (const auto &path : {tagged, second}) {
            const auto result = peer.call("library.import", {{"path", path}});
            QCOMPARE(result.value("status").toString(), QString("ok"));
            ids.append(result.value("data").toObject().value("song_id").toInt());
        }
        const auto metadata = peer.call("song.metadata", {{"song_id", ids[0].toInt()}}).value("data").toObject();
        QCOMPARE(metadata.value("album").toString(), QString("月光专辑"));
        QCOMPARE(metadata.value("album_artist").toString(), QString("Various Artists"));
        QCOMPARE(metadata.value("year").toInt(), 2024);
        QCOMPARE(metadata.value("track_number").toInt(), 2);
        for (const auto &patch : {QJsonObject{{"year", 2024.5}}, QJsonObject{{"year", "2024"}},
                                  QJsonObject{{"play_count", 999}}, QJsonObject{{"album", true}}}) {
            QCOMPARE(peer.call("library.update_metadata", {{"song_ids", QJsonArray::fromVariantList(ids)}, {"patch", patch}})
                         .value("status").toString(), QString("error"));
        }
        IpcClient client;
        AppControllers controllers(client);
        auto *library = controllers.library;
        QTRY_VERIFY(client.connected());
        QTRY_VERIFY(library->loaded() && !library->loading());
        library->setSearchText("月光专辑");
        QCOMPARE(library->filteredSongs()->count(), 1);
        library->playSongs(ids);
        QTRY_COMPARE(controllers.queue->model()->count(), 2);
        QCOMPARE(peer.call("player.seek", {{"position", 50}}).value("status").toString(), QString("ok"));
        QSignalSpy saved(library, &LibraryController::batchMetadataSaved);
        library->saveBatchMetadata(ids, {{"album", "月光专辑"}, {"album_artist", "Various Artists"},
            {"genre", "Ambient"}, {"year", 2026}, {"tags", QStringList{"Album batch integration"}}}, "add");
        QTRY_COMPARE(saved.count(), 1);
        QCOMPARE(saved.first().first().toInt(), 2);
        QTRY_COMPARE(library->filteredSongs()->count(), 2);
        QTRY_COMPARE(controllers.queue->model()->get(0).value("year").toInt(), 2026);
        QCOMPARE(controllers.queue->model()->get(0).value("album").toString(), QString("月光专辑"));
        QCOMPARE(controllers.queue->model()->get(0).value("track_number").toInt(), 2);
        const auto state = peer.call("player.status").value("data").toObject();
        QCOMPARE(state.value("position").toInt(), 50);
        QCOMPARE(state.value("song").toObject().value("song_id").toInt(), ids[0].toInt());
        QCOMPARE(peer.call("song.metadata", {{"song_id", ids[0].toInt()}}).value("data").toObject().value("lyrics").toString(), QString("Embedded lyrics"));
        library->setAlbumView(true);
        QCOMPARE(library->albums()->count(), 1);
        I18n translator;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("ipcClient", &client);
        engine.rootContext()->setContextProperty("controllers", &controllers);
        engine.rootContext()->setContextProperty("i18n", &translator);
        engine.rootContext()->setContextProperty("lyricsDebugEnabled", false);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QTRY_VERIFY(window->isVisible());
        window->setProperty("viewMode", "library");
        QVariant pageValue;
        QVERIFY(QMetaObject::invokeMethod(window, "pageItem", Q_RETURN_ARG(QVariant, pageValue), Q_ARG(QVariant, QVariant("library"))));
        auto *page = pageValue.value<QObject *>();
        QVERIFY(page);
        auto *panel = page->findChild<QQuickItem *>("libraryPanel");
        QVERIFY(panel);
        const auto screenshots = qEnvironmentVariable("NEKOTUNE_METADATA_SCREENSHOT_DIR");
        for (const QString language : {"zh", "en"}) {
            translator.setLanguage(language);
            window->setWidth(language == "zh" ? 1360 : 1000);
            window->setHeight(language == "zh" ? 860 : 640);
            library->closeAlbum();
            QTest::qWait(150);
            auto capture = [&](const QString &name) {
                if (!screenshots.isEmpty()) {
                    QTest::qWait(150);
                    QVERIFY(window->grabWindow().save(screenshots + "/" + name + "-" + language + ".png"));
                }
            };
            capture("albums");
            auto *card = visualItem(panel, "libraryAlbum0");
            QVERIFY(card && card->isVisible());
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, card->mapToScene(QPointF(card->width() / 2, card->height() / 2)).toPoint());
            QTRY_VERIFY(!library->selectedAlbumId().isEmpty());
            library->toggleSelectAll();
            QCOMPARE(library->selectedSongIds().size(), 2);
            capture("album-tracks");
            auto *button = panel->findChild<QQuickItem *>("libraryBatchEditButton");
            QVERIFY(button);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
            auto *dialog = panel->findChild<QObject *>("batchMetadataEditor");
            QVERIFY(dialog);
            QTRY_VERIFY(dialog->property("opened").toBool());
            capture("batch-metadata");
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        }
        runtime.stop();
    }
    void libraryViewOrderSurvivesPlaybackRefresh() {
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        QCOMPARE(peer.call("lyrics.set_offline", {{"offline", true}}).value("status").toString(), QString("ok"));
        QVariantList ids;
        for (int index = 0; index < 3; ++index) {
            const auto path = m_directory.filePath(QString("library-view-%1.wav").arg(index));
            // Distinct bytes and duration avoid reusing earlier tests' hash-deduplicated songs.
            writeAudio(path, char(110 + index), 31);
            const auto result = peer.call("library.import", {{"path", path}});
            QCOMPARE(result.value("status").toString(), QString("ok"));
            const int id = result.value("data").toObject().value("song_id").toInt();
            ids.append(id);
            QCOMPARE(peer.call("song.update_metadata", {{"song_id", id},
                {"custom_title", QString("Library view · 夜空 %1").arg(index + 1)},
                {"artist", "NekoTune"}, {"tags", QJsonArray{"Library view"}}}).value("status").toString(), QString("ok"));
        }
        IpcClient client;
        AppControllers controllers(client);
        auto *library = controllers.library;
        QTRY_VERIFY(client.connected());
        QTRY_VERIFY(library->loaded() && !library->loading());
        int tagId = 0;
        for (const auto &tag : library->tags()->items())
            if (tag.toMap().value("name").toString() == "Library view") tagId = tag.toMap().value("id").toInt();
        QVERIFY(tagId > 0);
        library->setSelectedTagIds({tagId});
        library->setImportSource("local");
        library->setPlayFilter("never_played");
        library->setSortOrder("added_desc");
        QCOMPARE(library->filteredSongs()->count(), 3);
        library->selectRow(ids[0].toInt());
        QSignalSpy played(library, &LibraryController::libraryPlaybackSkipped);
        library->playLibrary({tagId});
        QTRY_COMPARE(played.count(), 1);
        QTRY_COMPARE(library->filteredSongs()->count(), 2);
        auto queueIds = [&] {
            QVariantList result;
            for (const auto &song : controllers.queue->model()->items()) result.append(song.toMap().value("song_id"));
            return result;
        };
        const QVariantList expectedQueue{ids[2], ids[1], ids[0]};
        QTRY_COMPARE(queueIds(), expectedQueue);
        QTRY_COMPARE(controllers.playback->song().value("song_id").toInt(), ids[2].toInt());
        QCOMPARE(library->selectedSongIds(), QVariantList{ids[0]});
        QCOMPARE(library->sortOrder(), QString("added_desc"));
        QCOMPARE(library->importSource(), QString("local"));
        QCOMPARE(library->playFilter(), QString("never_played"));
        library->setPlayFilter("all");
        library->setSortOrder("most_played");
        QCOMPARE(library->filteredSongs()->get(0).value("song_id").toInt(), ids[2].toInt());
        QCOMPARE(library->filteredSongs()->get(0).value("play_count").toLongLong(), 1);
        QCOMPARE(peer.call("player.seek", {{"position", 12000}}).value("status").toString(), QString("ok"));

        I18n translator;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("ipcClient", &client);
        engine.rootContext()->setContextProperty("controllers", &controllers);
        engine.rootContext()->setContextProperty("i18n", &translator);
        engine.rootContext()->setContextProperty("lyricsDebugEnabled", false);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QTRY_VERIFY(window->isVisible());
        window->setProperty("viewMode", "library");
        QVariant pageValue;
        QVERIFY(QMetaObject::invokeMethod(window, "pageItem", Q_RETURN_ARG(QVariant, pageValue),
                                          Q_ARG(QVariant, QVariant("library"))));
        auto *page = pageValue.value<QObject *>();
        QVERIFY(page);
        auto *panel = page->findChild<QQuickItem *>("libraryPanel");
        QVERIFY(panel);
        const auto screenshotDirectory = qEnvironmentVariable("NEKOTUNE_LIBRARY_SCREENSHOT_DIR");
        for (const QString language : {"zh", "en"}) {
            translator.setLanguage(language);
            for (int width : {1360, 1000}) {
                window->setWidth(width);
                window->setHeight(width == 1000 ? 640 : 860);
                QTest::qWait(100);
                QQuickItem *previous = nullptr;
                for (const QString name : {"librarySortPicker", "libraryPlayFilterPicker", "librarySourcePicker"}) {
                    auto *picker = panel->findChild<QQuickItem *>(name);
                    QVERIFY(picker && picker->isVisible());
                    QVERIFY(picker->width() >= 150);
                    const auto point = picker->mapToItem(panel, QPointF());
                    QVERIFY(point.x() >= 0 && point.x() + picker->width() <= panel->width() + 1);
                    if (previous) QVERIFY(point.x() >= previous->mapToItem(panel, QPointF()).x() + previous->width());
                    previous = picker;
                }
                if (!screenshotDirectory.isEmpty()) {
                    QTest::qWait(150);
                    QVERIFY(window->grabWindow().save(screenshotDirectory + "/library-" + language + "-" + QString::number(width) + ".png"));
                }
            }
        }
        window->setProperty("viewMode", "home");
        window->setProperty("viewMode", "library");
        QCOMPARE(library->sortOrder(), QString("most_played"));
        QCOMPARE(library->selectedSongIds(), QVariantList{ids[0]});
        QCOMPARE(queueIds(), expectedQueue);
        auto state = peer.call("player.status").value("data").toObject();
        QCOMPARE(state.value("position").toInt(), 12000);
        QCOMPARE(state.value("state").toString(), QString("playing"));
        QCOMPARE(peer.call("player.next").value("status").toString(), QString("ok"));
        QTRY_COMPARE(library->filteredSongs()->get(0).value("song_id").toInt(), ids[1].toInt());
        QCOMPARE(library->selectedSongIds(), QVariantList{ids[0]});
        QCOMPARE(queueIds(), expectedQueue);
        runtime.stop();
    }
    // Home owns its recent/all-library playback context independently of another page.
    // Existing library text/tag filters must not narrow the home-requested sequence.
    // Single-click selection does not replace queue state before explicit activation.
    // Playing current media can toggle rather than rebuild the recent sequence.
    // The real QML/controller path ensures wrappers preserve this ownership rule.
    void homePlaybackIgnoresLibraryFilters() {
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        peer.call("lyrics.set_offline", {{"offline", true}});
        QVariantList ids;
        QStringList paths;
        for (int index = 0; index < 3; ++index) {
            const auto path = m_directory.filePath(QString("home-%1.wav").arg(index));
            writeAudio(path, char(70 + index));
            const auto result = peer.call("library.import", {{"path", path}});
            QCOMPARE(result.value("status").toString(), QString("ok"));
            ids.prepend(result.value("data").toObject().value("song_id").toInt());
            paths.append(path);
        }
        const auto tagId = peer.call("tag.create", {{"name", "Home playback filter"}})
                               .value("data").toObject().value("tag_id").toInt();
        QVERIFY(tagId > 0);
        IpcClient client;
        AppControllers controllers(client);
        auto *library = controllers.library;
        QSignalSpy loads(library, &LibraryController::loadStateChanged);
        QSignalSpy failures(library, &LibraryController::requestFailed);
        QSignalSpy skipped(library, &LibraryController::libraryPlaybackSkipped);
        QVERIFY(!library->loaded());
        QTRY_VERIFY(client.connected());
        QTRY_VERIFY(library->loaded());
        QTRY_VERIFY(!library->loading());
        QVERIFY(loads.count() >= 2);
        library->setSearchText("No matching song for this filter");
        library->setSelectedTagIds({tagId});
        library->setImportSource("nonmatching.provider");
        library->setPlayFilter("never_played");
        library->setSortOrder("most_played");
        QCOMPARE(library->filteredSongs()->count(), 0);
        library->playSongs(ids, ids.first().toInt());
        QTRY_COMPARE(skipped.count(), 1);
        QTRY_COMPARE(controllers.queue->model()->count(), 3);
        auto queueIds = [&] {
            QVariantList result;
            for (const auto &item : controllers.queue->model()->items())
                result.append(item.toMap().value("song_id").toInt());
            return result;
        };
        QCOMPARE(queueIds(), ids);
        QCOMPARE(library->searchText(), QString("No matching song for this filter"));
        QCOMPARE(library->selectedTagIds(), QVariantList{tagId});
        const auto previousSong = controllers.playback->song().value("song_id");
        library->playSongs({}, 0);
        QTRY_COMPARE(failures.count(), 1);
        QCOMPARE(queueIds(), ids);
        QCOMPARE(controllers.playback->song().value("song_id"), previousSong);
        library->playSongs({999999}, 0);
        QTRY_COMPARE(failures.count(), 2);
        QCOMPARE(queueIds(), ids);
        QVERIFY(QFile::remove(peer.call("song.metadata", {{"song_id", ids[1].toInt()}})["data"]
                                  .toObject()["first_path"]
                                  .toString()));
        library->playSongs(ids, ids.first().toInt());
        QTRY_COMPARE(skipped.count(), 2);
        QCOMPARE(skipped.last().first().toInt(), 1);
        QTRY_COMPARE(controllers.queue->model()->count(), 2);
        QCOMPARE(queueIds(), (QVariantList{ids[0], ids[2]}));
        const auto count = library->songs()->count();
        runtime.stop();
        QTRY_VERIFY(!client.connected());
        library->refreshLibrary();
        QVERIFY(!library->loading());
        QVERIFY(library->loaded());
        QCOMPARE(library->songs()->count(), count);
        QVERIFY(runtime.start());
        QTRY_VERIFY(client.connected());
        QTRY_VERIFY(!library->loading());
        QVERIFY(library->loaded());
        runtime.stop();
    }
    void recentPlaybackTimestampOverIpc() {
        const auto path = m_directory.filePath("recent-playback.wav");
        writeAudio(path, char(73), 72);
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        QCOMPARE(peer.call("queue.clear").value("status").toString(), QString("ok"));
        const auto queued = peer.call("queue.add", {{"path", path}});
        QCOMPARE(queued.value("status").toString(), QString("ok"));
        const auto row = queued.value("data").toObject().value("items").toArray().first().toObject();
        const int id = row.value("song_id").toInt();
        auto metadata = [&] { return peer.call("song.metadata", {{"song_id", id}}).value("data").toObject(); };
        QVERIFY(metadata().value("last_played_at").toString().isEmpty());
        QCOMPARE(metadata().value("play_count").toInt(), 0);
        IpcClient client;
        AppControllers controllers(client);
        client.connectBackend();
        QTRY_VERIFY(client.connected());
        auto *library = controllers.library;
        QTRY_VERIFY(library->loaded());
        library->setSortOrder("recently_played");
        library->setSearchText("recent");
        library->loadMetadata(id); // Loading the editing preview must not count as playback.
        QTRY_VERIFY(library->metadataReady());
        QVERIFY(metadata().value("last_played_at").toString().isEmpty());
        const auto started = peer.call("queue.play", {{"id", row.value("queue_id")}});
        QCOMPARE(started.value("status").toString(), QString("ok"));
        const auto stamp = started.value("data").toObject().value("song").toObject().value("last_played_at").toString();
        QVERIFY(QDateTime::fromString(stamp, Qt::ISODate).isValid());
        QCOMPARE(metadata().value("last_played_at").toString(), stamp);
        auto frontendTime = [&] {
            for (const auto &value : library->songs()->items()) {
                const auto song = value.toMap();
                if (song.value("song_id").toInt() == id) return song.value("last_played_at").toString();
            }
            return QString();
        };
        QTRY_COMPARE(frontendTime(), stamp);
        library->setPlayFilter("last_7_days");
        library->setSearchText("");
        library->loadMetadata(id);
        QTRY_VERIFY(library->metadataReady());
        QCOMPARE(peer.call("player.pause").value("status").toString(), QString("ok"));
        QCOMPARE(peer.call("player.play").value("status").toString(), QString("ok"));
        QCOMPARE(peer.call("player.seek", {{"position", 500}}).value("status").toString(), QString("ok"));
        QCOMPARE(metadata().value("last_played_at").toString(), stamp);
        QCOMPARE(metadata().value("play_count").toInt(), 1);
        QCOMPARE(peer.call("library.update_metadata", {{"song_ids", QJsonArray{id}},
                                                       {"patch", QJsonObject{{"last_played_at", "2000-01-01T00:00:00.000Z"}}}})
                     .value("status").toString(), QString("error"));
        runtime.stop();
        QTRY_VERIFY(!client.connected());
        QVERIFY(runtime.start());
        RpcPeer restored;
        QVERIFY(restored.connect(m_socket));
        const auto saved = restored.call("song.metadata", {{"song_id", id}}).value("data").toObject();
        QCOMPARE(saved.value("last_played_at").toString(), stamp);
        QCOMPARE(saved.value("play_count").toInt(), 1);
        runtime.stop();
    }
    // Batch insertion and reordering must use occurrence identities and reject invalid inputs atomically.
    void queueInsertionAndMoveIpc() {
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        auto call = [&](const QString &method, const QJsonObject &params = {}) {
            return peer.call(method, params);
        };
        QCOMPARE(call("queue.clear").value("status").toString(), QString("ok"));
        auto added = call("queue.add", {{"path", m_audio}});
        QCOMPARE(added.value("status").toString(), QString("ok"));
        const auto first = added.value("data").toObject().value("items").toArray().first().toObject();
        const int songId = first.value("song_id").toInt(), firstId = first.value("queue_id").toInt();
        QCOMPARE(call("queue.play", {{"id", firstId}}).value("status").toString(), QString("ok"));
        auto batch = call("queue.add", {{"song_ids", QJsonArray{songId, songId}}, {"next", true}});
        QCOMPARE(batch.value("status").toString(), QString("ok"));
        auto items = batch.value("data").toObject().value("items").toArray();
        QCOMPARE(items.size(), 3);
        const int priority = items[1].toObject().value("queue_id").toInt();
        QVERIFY(priority != firstId);
        QVERIFY(priority != items[2].toObject().value("queue_id").toInt());
        QCOMPARE(call("queue.move", {{"queue_id", firstId}, {"position", 2}}).value("status").toString(), QString("ok"));
        QCOMPARE(call("queue.status").value("data").toObject().value("current_index").toInt(), 2);
        auto positioned = call("queue.add", {{"song_id", songId}, {"position", 0}});
        QCOMPARE(positioned.value("status").toString(), QString("ok"));
        QCOMPARE(positioned.value("data").toObject().value("current_index").toInt(), 3);
        const int oldSize = positioned.value("data").toObject().value("items").toArray().size();
        for (const auto &bad : {QJsonObject{{"path", m_audio}, {"position", -1}},
                               QJsonObject{{"song_id", songId}, {"position", 1.5}},
                               QJsonObject{{"song_id", songId}, {"position", oldSize + 1}},
                               QJsonObject{{"song_id", songId}, {"next", "true"}},
                               QJsonObject{{"song_id", songId}, {"next", true}, {"position", 0}},
                               QJsonObject{{"path", m_audio}, {"song_id", songId}},
                               QJsonObject{{"song_ids", QJsonArray{songId, -1}}},
                               QJsonObject{{"song_ids", QJsonArray{songId, 999999}}},
                               QJsonObject{{"song_ids", QJsonArray{}}}})
            QCOMPARE(call("queue.add", bad).value("status").toString(), QString("error"));
        for (const auto &bad : {QJsonObject{{"queue_id", firstId}, {"position", -1}},
                               QJsonObject{{"queue_id", firstId}, {"position", .5}},
                               QJsonObject{{"queue_id", firstId}, {"position", oldSize}},
                               QJsonObject{{"queue_id", 999999}, {"position", 0}},
                               QJsonObject{{"id", firstId}, {"position", 0}}})
            QCOMPARE(call("queue.move", bad).value("status").toString(), QString("error"));
        QCOMPARE(call("queue.status").value("data").toObject().value("items").toArray().size(), oldSize);
        IpcClient client;
        QueueController controller(client);
        client.connectBackend();
        QTRY_VERIFY(client.connected());
        QTRY_COMPARE(controller.model()->count(), oldSize);
        controller.moveQueueItem(firstId, 0);
        QTRY_COMPARE(controller.model()->get(0).value("queue_id").toInt(), firstId);
        auto pathNext = call("queue.add", {{"path", m_audio}, {"next", true}});
        QCOMPARE(pathNext.value("status").toString(), QString("ok"));
        const auto beforeRestart = pathNext.value("data").toObject().value("items").toArray();
        QVERIFY(beforeRestart[1].toObject().value("play_next").toBool());
        runtime.stop();
        QTRY_VERIFY(!client.connected());
        QVERIFY(runtime.start());
        RpcPeer restored;
        QVERIFY(restored.connect(m_socket));
        const auto afterRestart = restored.call("queue.status").value("data").toObject();
        QCOMPARE(afterRestart.value("current_index").toInt(), 0);
        const auto restoredItems = afterRestart.value("items").toArray();
        QCOMPARE(restoredItems.size(), beforeRestart.size());
        for (int i = 0; i < restoredItems.size(); ++i) {
            QCOMPARE(restoredItems[i].toObject().value("path"), beforeRestart[i].toObject().value("path"));
            QCOMPARE(restoredItems[i].toObject().value("play_next"), beforeRestart[i].toObject().value("play_next"));
        }
        QCOMPARE(restored.call("player.set_playback_mode", {{"mode", "shuffle"}}).value("status").toString(), QString("ok"));
        const int restoredPriority = restoredItems[1].toObject().value("queue_id").toInt();
        const auto advanced = restored.call("player.next");
        QCOMPARE(advanced.value("status").toString(), QString("ok"));
        QCOMPARE(advanced.value("data").toObject().value("song").toObject().value("queue_id").toInt(), restoredPriority);
        runtime.stop();
    }
    // Playback detail, bottom controls and the queue drawer share one confirmed mode.
    // Opening different surfaces must not reset the current occurrence or progress.
    // Mode request failure/disconnection must leave the previous authoritative selection.
    // Menu behavior is exercised through actual QML bindings and backend state.
    void playbackModePageAndDrawer() {
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        QCOMPARE(peer.call("lyrics.set_offline", {{"offline", true}}).value("status").toString(), QString("ok"));
        QCOMPARE(peer.call("queue.clear").value("status").toString(), QString("ok"));
        QCOMPARE(peer.call("queue.add", {{"path", m_audio}}).value("status").toString(), QString("ok"));
        IpcClient client;
        AppControllers controllers(client);
        I18n translator;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("ipcClient", &client);
        engine.rootContext()->setContextProperty("controllers", &controllers);
        engine.rootContext()->setContextProperty("i18n", &translator);
        engine.rootContext()->setContextProperty("lyricsDebugEnabled", false);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QTRY_VERIFY(client.connected());
        QTRY_VERIFY(window->isVisible());
        window->setProperty("viewMode", "queue");
        window->setProperty("currentPlaylist", 0);
        QVariant queuePageValue;
        QVERIFY(QMetaObject::invokeMethod(window, "pageItem", Q_RETURN_ARG(QVariant, queuePageValue),
                                          Q_ARG(QVariant, QVariant("queue"))));
        auto *queuePage = queuePageValue.value<QObject *>();
        QVERIFY(queuePage);
        auto *panel = queuePage->findChild<QQuickItem *>("queuePanel");
        auto *drawer = window->findChild<QQuickItem *>("queueDrawerPanel");
        QVERIFY(panel && drawer);
        auto *pageButton = panel->findChild<QQuickItem *>("playbackModeButton");
        auto *drawerButton = drawer->findChild<QQuickItem *>("playbackModeButton");
        QVERIFY(pageButton && drawerButton);
        const auto screenshotDirectory = qEnvironmentVariable("NEKOTUNE_PLAYBACK_SCREENSHOT_DIR");
        for (const QString language : {"zh", "en"}) {
            translator.setLanguage(language);
            window->setWidth(language == "zh" ? 1360 : 1000);
            for (const QString mode : {"sequential", "repeat_one", "shuffle", "repeat_all"}) {
                QCOMPARE(peer.call("player.set_playback_mode", {{"mode", mode}}).value("status").toString(), QString("ok"));
                QTRY_COMPARE(pageButton->property("kind").toString(), mode);
                QTRY_COMPARE(drawerButton->property("kind").toString(), mode);
                for (bool compact : {false, true}) {
                    window->setProperty("queueOpen", compact);
                    auto *button = compact ? drawerButton : pageButton;
                    QTest::qWait(50);
                    const QPoint point = button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
                    auto *menu = button->findChild<QObject *>("playbackModeMenu");
                    QVERIFY(menu);
                    QTRY_VERIFY(menu->property("opened").toBool());
                    auto *content = menu->property("contentItem").value<QQuickItem *>();
                    QVERIFY(content);
                    auto *choice = visualItem(content, "playbackMode_" + mode);
                    QVERIFY(choice);
                    QVERIFY(choice->property("selected").toBool());
                    QVERIFY(menu->property("x").toDouble() >= 8);
                    QVERIFY(menu->property("y").toDouble() >= 8);
                    if (!screenshotDirectory.isEmpty()) {
                        QTest::qWait(150);
                        const auto path = screenshotDirectory + "/playback-" + language + "-" + mode +
                                          (compact ? "-drawer.png" : "-page.png");
                        QVERIFY(window->grabWindow().save(path));
                    }
                    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
                    QTRY_VERIFY(!menu->property("visible").toBool());
                }
            }
        }
        // Exercise a menu selection through the actual IPC/controller binding.
        window->setProperty("queueOpen", true);
        QTest::qWait(50);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                         drawerButton->mapToScene(QPointF(drawerButton->width() / 2, drawerButton->height() / 2)).toPoint());
        auto *menu = drawerButton->findChild<QObject *>("playbackModeMenu");
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *choice = visualItem(menu->property("contentItem").value<QQuickItem *>(), "playbackMode_shuffle");
        QVERIFY(choice);
        QTest::qWait(50);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                         choice->mapToScene(QPointF(choice->width() / 2, choice->height() / 2)).toPoint());
        QTRY_COMPARE(controllers.playback->playbackMode(), QString("shuffle"));
        QCOMPARE(peer.call("player.status").value("data").toObject().value("playback_mode").toString(), QString("shuffle"));
        runtime.stop();
    }
    // Exercise animation interruption and real hit areas rather than checking token values alone.
    void drawerAnimationAndCompactControls() {
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        peer.call("lyrics.set_offline", {{"offline", true}});
        IpcClient client;
        AppControllers controllers(client);
        I18n translator;
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings.append(error.toString());
        });
        engine.rootContext()->setContextProperty("ipcClient", &client);
        engine.rootContext()->setContextProperty("controllers", &controllers);
        engine.rootContext()->setContextProperty("i18n", &translator);
        engine.rootContext()->setContextProperty("lyricsDebugEnabled", false);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QTRY_VERIFY(client.connected());
        QTRY_VERIFY(window->isVisible());
        auto *drawer = window->findChild<QQuickItem *>("queueDrawer");
        auto *secondary = window->findChild<QQuickItem *>("playerSecondaryControls");
        auto *transport = window->findChild<QQuickItem *>("transportControls");
        QVERIFY(drawer && secondary && transport);
        QVERIFY(!drawer->isVisible());
        QVERIFY(!drawer->isEnabled());

        window->setProperty("queueOpen", true);
        QTRY_VERIFY(drawer->opacity() > 0 && drawer->opacity() < 1);
        window->setProperty("queueOpen", false);
        QVERIFY(drawer->isVisible());
        QVERIFY(!drawer->isEnabled());
        window->setProperty("queueOpen", true);
        QTRY_COMPARE(drawer->opacity(), 1.0);
        QTRY_COMPARE(drawer->x() + drawer->width(), qreal(window->width()));
        QVERIFY(drawer->isEnabled());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("queueOpen").toBool());
        QVERIFY(!drawer->isEnabled());
        QTRY_VERIFY(!drawer->isVisible());
        QCOMPARE(drawer->opacity(), 0.0);

        for (const QString language : {"zh", "en"}) {
            translator.setLanguage(language);
            for (int width : {1000, 1360}) {
                window->resize(width, width == 1000 ? 640 : 860);
                QTRY_COMPARE(window->width(), width);
                QTest::qWait(60);
                QCOMPARE(secondary->width(), width == 1000 ? 160.0 : 220.0);
                QCOMPARE(transport->mapToScene(QPointF(transport->width() / 2, 0)).x(), width / 2.0);
                const QRectF bounds(0, 0, secondary->width(), secondary->height());
                QList<QRectF> controlRects;
                for (const QString name : {"volumeMuteButton", "volumeSlider", "audioOutputButton", "nowPlayingButton", "queueToggleButton"}) {
                    auto *control = secondary->findChild<QQuickItem *>(name);
                    QVERIFY2(control, qPrintable(name));
                    const QRectF rect(control->mapToItem(secondary, QPointF()), control->size());
                    QVERIFY2(bounds.contains(rect), qPrintable(name));
                    for (const auto &other : controlRects)
                        QVERIFY2(!rect.intersects(other), qPrintable(name));
                    controlRects.append(rect);
                    if (name == "volumeSlider")
                        QVERIFY(control->width() >= 72);
                }
                auto *toggle = secondary->findChild<QQuickItem *>("queueToggleButton");
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                                 toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());
                QTRY_COMPARE(drawer->opacity(), 1.0);
                QCOMPARE(drawer->width(), width == 1000 ? 360.0 : 400.0);
                window->setProperty("queueOpen", false);
                QTRY_VERIFY(!drawer->isVisible());
            }
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        runtime.stop();
    }
    // Use loaded QML to verify controller/model separation under real events.
    // Metadata editing must request full fields before saving sparse changes.
    // Library selection should survive playback progress and metadata reconciliation.
    // Muting is explicit zero and unmuting restores the remembered positive level.
    // The test also exercises dialog lifecycle and current-row action identity.
    // It does not substitute for a separate screenshot-based visual acceptance.
    void extensionPluginNavigation() {
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        QTRY_VERIFY_WITH_TIMEOUT(
            peer.call("extensions.list").value("data").toObject().value("runtime").toObject().value("ready").toBool(), 15000);
        const auto installed = peer.call("extensions.install",
            {{"path", QStringLiteral(NEKOTUNE_TEST_EXTENSIONS "/test.dedicated-source")},
             {"development", true}, {"replace", true}});
        QCOMPARE(installed.value("status").toString(), QString("ok"));
        IpcClient client;
        AppControllers controllers(client);
        I18n translator;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("ipcClient", &client);
        engine.rootContext()->setContextProperty("controllers", &controllers);
        engine.rootContext()->setContextProperty("i18n", &translator);
        engine.rootContext()->setContextProperty("lyricsDebugEnabled", false);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QTRY_VERIFY(client.connected());
        QVERIFY(!visualItem(window->contentItem(), "nav_test.dedicated-source/music"));
        const auto enabled = peer.call("extensions.enable", {{"id", "test.dedicated-source"}, {"trusted", true}});
        QVERIFY2(enabled.value("status") == "ok",
                 qPrintable(QString::fromUtf8(QJsonDocument(enabled).toJson())));
        QTRY_VERIFY(visualItem(window->contentItem(), "nav_test.dedicated-source/music"));
        QVERIFY(!visualItem(window->contentItem(), "nav_test.dedicated-source/settings"));
        QCOMPARE(controllers.extensions->settingsPages().size(), 1);
        QTRY_COMPARE(controllers.lyrics->sources().size(), 2);
        QVERIFY(controllers.extensions->browserSources().isEmpty());
        QVERIFY(!visualItem(window->contentItem(), "nav_music_sources"));
        auto pluginPage = [&]() -> QObject * {
            QVariant result;
            QMetaObject::invokeMethod(window, "pageItem", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, QVariant("test.dedicated-source/music")));
            return result.value<QObject *>();
        };
        QVERIFY(!pluginPage());
        auto *button = visualItem(window->contentItem(), "nav_test.dedicated-source/music");
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QCOMPARE(window->property("viewMode").toString(), QString("test.dedicated-source/music"));
        QTRY_VERIFY_WITH_TIMEOUT(pluginPage(), 5000);
        QPointer<QObject> retainedPluginPage = pluginPage();
        QCOMPARE(pluginPage()->objectName(), QString("fixturePluginPage"));
        QVERIFY(pluginPage()->findChild<QObject *>("fixturePluginPanel"));
        const auto screenshotDirectory = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT_DIR");
        if (!screenshotDirectory.isEmpty()) {
            window->resize(1000, 720);
            for (const QString language : {"zh", "en"}) {
                translator.setLanguage(language);
                QTest::qWait(180);
                QVERIFY(
                    window->grabWindow().save(screenshotDirectory + "/fixture-plugin-" + language + ".png"));
            }
        }
        auto *settingsNav = visualItem(window->contentItem(), "nav_settings");
        QVERIFY(settingsNav);
        QVERIFY(QMetaObject::invokeMethod(settingsNav, "clicked"));
        QCOMPARE(window->property("viewMode").toString(), QString("settings"));
        QCOMPARE(pluginPage(), retainedPluginPage.data());
        auto entry = [&] { return visualItem(window->contentItem(), "settings_entry_test.dedicated-source/settings"); };
        QTRY_VERIFY(entry() && entry()->isVisible());
        for (const QString language : {"zh", "en"}) {
            translator.setLanguage(language);
            QTRY_COMPARE(entry()->property("text").toString(),
                         language == "zh" ? QString("测试设置") : QString("Test settings"));
            if (!screenshotDirectory.isEmpty()) {
                QTest::qWait(180);
                QVERIFY(window->grabWindow().save(screenshotDirectory + "/settings-overview-" + language + ".png"));
            }
            QVERIFY(QMetaObject::invokeMethod(entry(), "clicked"));
            QCOMPARE(window->property("viewMode").toString(), QString("settings"));
            QCOMPARE(window->property("settingsSection").toString(), QString("test.dedicated-source/settings"));
            QTRY_VERIFY(visualItem(window->contentItem(), "fixturePluginSettings"));
            auto *title = visualItem(window->contentItem(), "settingsSubpageTitle");
            QVERIFY(title);
            QCOMPARE(title->property("text").toString(), entry()->property("text").toString());
            if (!screenshotDirectory.isEmpty()) {
                QTest::qWait(180);
                QVERIFY(window->grabWindow().save(screenshotDirectory + "/settings-fixture-subpage-" + language + ".png"));
            }
            auto *back = visualItem(window->contentItem(), "settingsBackButton");
            QVERIFY(back && back->isVisible());
            QVERIFY(QMetaObject::invokeMethod(back, "clicked"));
            QCOMPARE(window->property("settingsSection").toString(), QString());
            QTRY_VERIFY(!visualItem(window->contentItem(), "fixturePluginSettings"));
            QVERIFY(entry()->isVisible());
        }
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QCOMPARE(pluginPage(), retainedPluginPage.data());
        auto *shortcut = visualItem(window->contentItem(), "fixtureOpenSettingsButton");
        QVERIFY(shortcut && shortcut->isVisible());
        QVERIFY(QMetaObject::invokeMethod(shortcut, "clicked"));
        QCOMPARE(window->property("viewMode").toString(), QString("settings"));
        QTRY_VERIFY(visualItem(window->contentItem(), "fixturePluginSettings"));
        const auto disabled = peer.call("extensions.disable", {{"id", "test.dedicated-source"}});
        QCOMPARE(disabled.value("status").toString(), QString("ok"));
        QTRY_VERIFY(!visualItem(window->contentItem(), "nav_test.dedicated-source/music"));
        QTRY_COMPARE(controllers.lyrics->sources().size(), 1);
        QTRY_COMPARE(window->property("settingsSection").toString(), QString());
        QCOMPARE(window->property("viewMode").toString(), QString("settings"));
        QTRY_VERIFY(!entry());
        QTRY_VERIFY(!visualItem(window->contentItem(), "fixturePluginSettings"));
        runtime.stop();
    }

    void realQmlMetadataSelectionAndMute() {
        QImage coverImage(40, 40, QImage::Format_RGB32);
        coverImage.fill(QColor("#E8A9C3"));
        if (!qEnvironmentVariable("NEKOTUNE_HOME_SCREENSHOT").isEmpty())
            QVERIFY(coverImage.load(":/artwork/default-cover.png"));
        const auto coverPath = m_directory.filePath("test.png");
        QVERIFY(coverImage.save(coverPath));
        BackendRuntime runtime(makeFakeAudio);
        QVERIFY(runtime.start());
        RpcPeer peer;
        QVERIFY(peer.connect(m_socket));
        peer.call("lyrics.set_offline", {{"offline", true}});
        auto imported = peer.call("library.import", {{"path", m_audio}});
        const int songId = imported.value("data").toObject().value("song_id").toInt();
        QVERIFY(songId > 0);
        peer.call("song.update_metadata", {{"song_id", songId}, {"lyrics", "Keep my custom lyrics"}});
        IpcClient client;
        AppControllers controllers(client);
        I18n translator;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("ipcClient", &client);
        engine.rootContext()->setContextProperty("controllers", &controllers);
        engine.rootContext()->setContextProperty("i18n", &translator);
        engine.rootContext()->setContextProperty("lyricsDebugEnabled", true);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *root = engine.rootObjects().first();
        QTRY_VERIFY(client.connected());
        QTRY_VERIFY(controllers.library->songs()->count() > 0);
        QCOMPARE(root->property("viewMode").toString(), QString("home"));
        QVERIFY(root->property("nowPlayingOpen").toBool() == false);
        auto *window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        QVERIFY(visualItem(window->contentItem(), "nav_home"));
        QVariant homeValue;
        QVERIFY(QMetaObject::invokeMethod(root, "pageItem", Q_RETURN_ARG(QVariant, homeValue),
                                          Q_ARG(QVariant, QVariant("home"))));
        auto *home = homeValue.value<QObject *>();
        QVERIFY(home);
        // Cold startup only constructs the landing page, even when debug is enabled.
        for (const QString page : {"extensions", "history", "library", "music_sources", "queue",
                                   "lyrics", "settings", "lyrics_debug"}) {
            QVariant item, status;
            QVERIFY(QMetaObject::invokeMethod(root, "pageItem", Q_RETURN_ARG(QVariant, item),
                                              Q_ARG(QVariant, QVariant(page))));
            QVERIFY(!item.value<QObject *>());
            QVERIFY(QMetaObject::invokeMethod(root, "pageStatus", Q_RETURN_ARG(QVariant, status),
                                              Q_ARG(QVariant, QVariant(page))));
            QCOMPARE(status.toInt(), 0); // Loader.Null
        }
        home->setProperty("recentView", "played");
        root->setProperty("viewMode", "library");

        controllers.library->toggleSelection(songId);
        QCOMPARE(controllers.library->selectedSongIds().size(), 1);
        peer.call("player.play", {{"path", m_audio}});
        QTest::qWait(300);
        auto *coverWindow = qobject_cast<QQuickWindow *>(root);
        QVERIFY(coverWindow);
        QQuickItem *coverRow = nullptr;
        QTRY_VERIFY(
            (coverRow = visualItem(coverWindow->contentItem(), "libraryRow" + QString::number(songId))));
        auto *rowCover = coverRow->findChild<QQuickItem *>("trackCover");
        auto *playerCover = root->findChild<QQuickItem *>("nowPlayingCover");
        QVERIFY(rowCover && playerCover);
        const auto coverUrl = QUrl::fromLocalFile(coverPath);
        QTRY_VERIFY(rowCover->property("source").toUrl().toLocalFile().contains("/artwork/"));
        const auto ownedCoverUrl = rowCover->property("source").toUrl();
        QCOMPARE(LibraryFileIO::hashFile(ownedCoverUrl.toLocalFile()).value(),
                 LibraryFileIO::hashFile(coverUrl.toLocalFile()).value());
        QTRY_COMPARE(playerCover->property("source").toUrl(), ownedCoverUrl);
        QTRY_COMPARE(rowCover->property("status").toInt(), 1); // Image.Ready
        QTRY_COMPARE(playerCover->property("status").toInt(), 1);
        const auto coverScreenshot = qEnvironmentVariable("NEKOTUNE_COVER_SCREENSHOT");
        if (!coverScreenshot.isEmpty()) {
            QTest::qWait(100);
            QVERIFY(coverWindow->grabWindow().save(coverScreenshot));
        }
        QCOMPARE(controllers.library->selectedSongIds().size(), 1);
        root->setProperty("viewMode", "home");
        QVariant retainedHome;
        QVERIFY(QMetaObject::invokeMethod(root, "pageItem", Q_RETURN_ARG(QVariant, retainedHome),
                                          Q_ARG(QVariant, QVariant("home"))));
        QCOMPARE(retainedHome.value<QObject *>(), home);
        QCOMPARE(home->property("recentView").toString(), QString("played"));
        home->setProperty("recentView", "added");
        root->setProperty("width", 1000);
        root->setProperty("height", 640);
        QTest::qWait(50);
        home->setProperty("contentY", 40);
        QVERIFY(QMetaObject::invokeMethod(root, "openNowPlaying"));
        QVERIFY(root->property("nowPlayingOpen").toBool());
        QTest::keyClick(coverWindow, Qt::Key_Escape);
        QTRY_VERIFY(!root->property("nowPlayingOpen").toBool());
        QCOMPARE(root->property("viewMode").toString(), QString("home"));
        QCOMPARE(home->property("contentY").toInt(), 40);
        QVERIFY(QMetaObject::invokeMethod(home, "newPlaylist"));
        QTRY_VERIFY(root->property("popupActive").toBool());
        QCOMPARE(root->property("viewMode").toString(), QString("home"));
        QVariant queuePageValue;
        QVERIFY(QMetaObject::invokeMethod(root, "queuePage", Q_RETURN_ARG(QVariant, queuePageValue)));
        auto *newPlaylistPopup = queuePageValue.value<QObject *>()->findChild<QObject *>("playlistNamePopup");
        QVERIFY(newPlaylistPopup);
        QVERIFY(QMetaObject::invokeMethod(newPlaylistPopup, "close"));
        QTRY_VERIFY(!root->property("popupActive").toBool());
        root->setProperty("width", 1360);
        root->setProperty("height", 860);
        controllers.playback->setVolume(0);
        QTRY_COMPARE(controllers.playback->volume(), 0.0);
        QCOMPARE(root->property("volume").toDouble(), 0.0);
        root->setProperty("viewMode", "music_sources");
        QVERIFY(QMetaObject::invokeMethod(root, "openNowPlaying"));
        QVERIFY(root->property("nowPlayingOpen").toBool());
        QCOMPARE(root->property("viewMode").toString(), QString("music_sources"));
        root->setProperty("queueOpen", true);
        QVERIFY(root->findChild<QQuickItem *>("queueDrawer")->isVisible());
        QVERIFY(QMetaObject::invokeMethod(root, "closeNowPlaying"));
        QCOMPARE(root->property("viewMode").toString(), QString("music_sources"));
        QVERIFY(!root->property("queueOpen").toBool());
        auto *controls = root->findChild<QQuickItem *>("transportControls");
        QVERIFY(controls);
        for (int width : {1000, 1360}) {
            root->setProperty("width", width);
            QTRY_COMPARE(root->property("width").toInt(), width);
            QTest::qWait(30);
            QCOMPARE(qRound(controls->mapToScene(QPointF(controls->width() / 2, 0)).x()), width / 2);
        }
        root->setProperty("viewMode", "library");

        controllers.playback->toggleMute();
        QTRY_VERIFY(controllers.playback->volume() > 0);
        QTRY_COMPARE(controllers.lyrics->current().value("state").toString(), QString("ready"));
        auto *editor = root->findChild<QObject *>("metadataEditor");
        QVERIFY(editor);
        const QVariant record = QVariantMap{{"song_id", songId}, {"title", "Incomplete list row"}};
        QVERIFY(QMetaObject::invokeMethod(editor, "openForSong", Q_ARG(QVariant, record)));
        QTRY_VERIFY(editor->property("tagsReady").toBool());
        QTRY_VERIFY(root->property("popupActive").toBool());
        auto *title = editor->findChild<QObject *>("metadataTitleField");
        auto *lyrics = editor->findChild<QObject *>("metadataLyricsField");
        QVERIFY(title && lyrics);
        QCOMPARE(lyrics->property("text").toString(), QString("Keep my custom lyrics"));
        QCOMPARE(controllers.lyrics->current().value("document").toMap().value("plain_text").toString(),
                 QString("Keep my custom lyrics"));
        title->setProperty("text", "Edited through QML");
        QVERIFY(QMetaObject::invokeMethod(editor, "save"));
        QTRY_VERIFY(!editor->property("visible").toBool());
        QTRY_VERIFY(!root->property("popupActive").toBool());
        auto metadata = peer.call("song.metadata", {{"song_id", songId}}).value("data").toObject();
        QCOMPARE(metadata.value("custom_title").toString(), QString("Edited through QML"));
        QCOMPARE(metadata.value("lyrics").toString(), QString("Keep my custom lyrics"));
        auto *uiWindow = qobject_cast<QQuickWindow *>(root);
        QVERIFY(uiWindow);
        const auto firstId =
            controllers.library->filteredSongs()->items().first().toMap().value("song_id").toInt();
        auto *firstRow = visualItem(uiWindow->contentItem(), "libraryRow" + QString::number(firstId));
        QVERIFY(firstRow);
        QSignalSpy selectionBehindDrawer(firstRow, SIGNAL(selectedRequested(int)));
        QVERIFY(selectionBehindDrawer.isValid());
        root->setProperty("queueOpen", true);
        QTest::qWait(30);
        auto *drawer = root->findChild<QQuickItem *>("queueDrawer");
        const auto point = QPoint(qRound(drawer->mapToScene(QPointF(4, 0)).x()),
                                  qRound(firstRow->mapToScene(QPointF(0, firstRow->height() / 2)).y()));
        QTest::mouseClick(uiWindow, Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(selectionBehindDrawer.count(), 0);
        root->setProperty("queueOpen", false);
        for (const QString page :
             {"home", "queue", "library", "lyrics", "music_sources", "settings", "lyrics_debug"}) {
            root->setProperty("viewMode", page);
            QTest::qWait(30);
            QVariant status;
            QVERIFY(QMetaObject::invokeMethod(root, "pageStatus", Q_RETURN_ARG(QVariant, status),
                                              Q_ARG(QVariant, QVariant(page))));
            QCOMPARE(status.toInt(), 1);
        }
        // Registration alone is enough to expose a new page; Main.qml has no
        // feature branch for it.
        const QString extraQml = "import QtQuick; Item { required property var shell; required "
                                 "property var controllers; required "
                                 "property var transport; required property var translator; signal "
                                 "importRequested(int id); "
                                 "signal editRequested(var song) }";
        auto pagesValue = root->property("pages");
        auto pages = pagesValue.value<QJSValue>().toVariant().toList();
        QVERIFY(!pages.isEmpty());
        pages.append(QVariantMap{
            {"id", "extra"},
            {"title", "extra"},
            {"source", "data:text/plain," + QString::fromLatin1(QUrl::toPercentEncoding(extraQml))}});
        root->setProperty("pages", pages);
        QTest::qWait(100);
        QVariant extraStatus;
        QVERIFY(QMetaObject::invokeMethod(root, "pageStatus", Q_RETURN_ARG(QVariant, extraStatus),
                                          Q_ARG(QVariant, QVariant("extra"))));
        QCOMPARE(extraStatus.toInt(), 0);
        root->setProperty("viewMode", "extra");
        QVERIFY(QMetaObject::invokeMethod(root, "pageStatus", Q_RETURN_ARG(QVariant, extraStatus),
                                          Q_ARG(QVariant, QVariant("extra"))));
        QCOMPARE(extraStatus.toInt(), 1);
        root->setProperty("viewMode", "home");
        pages.removeLast();
        root->setProperty("pages", pages);
        auto tag = peer.call("tag.create", {{"name", "Regression"}})
                       .value("data")
                       .toObject()
                       .value("tag_id")
                       .toInt();
        QVERIFY(tag > 0);
        peer.call("song.update_metadata", {{"song_id", songId}, {"tags", QJsonArray{"Regression"}}});
        QTRY_VERIFY(controllers.library->tags()->count() > 0);
        controllers.library->toggleTag(tag);
        QTRY_COMPARE(controllers.library->filteredSongs()->count(), 1);
        controllers.library->setSelectedTagIds({});
        const auto homeScreenshot = qEnvironmentVariable("NEKOTUNE_HOME_SCREENSHOT");
        const auto screenshot = homeScreenshot.isEmpty() ? qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT") : homeScreenshot;
        if (!screenshot.isEmpty()) {
            // The registry extension test recreated the page loaders above.
            QVERIFY(QMetaObject::invokeMethod(root, "pageItem", Q_RETURN_ARG(QVariant, homeValue),
                                              Q_ARG(QVariant, QVariant("home"))));
            home = homeValue.value<QObject *>();
            QVERIFY(home);
            auto *window = qobject_cast<QQuickWindow *>(root);
            QVERIFY(window);
            const auto playlistId = peer.call("playlist.create", {{"name", "夜色收藏 · Night collection"}})
                                        .value("data")
                                        .toObject()
                                        .value("playlist_id")
                                        .toInt();
            const auto emptyPlaylistId = peer.call("playlist.create", {{"name", "新的歌单 · New playlist"}})
                                             .value("data")
                                             .toObject()
                                             .value("playlist_id")
                                             .toInt();
            const QStringList titles{"夜に駆ける",
                                     "月光下的归途",
                                     "A quiet evening, a very long song title that should stay readable",
                                     "星屑と小さな猫",
                                     "雨后的城市 · Acoustic version",
                                     "晚风与耳机"};
            const QStringList artists{"YOASOBI",  "NekoTune",       "Night Radio Ensemble",
                                      "月夜楽団", "Indie Sessions", "夜樱电台"};
            for (int index = 0; index < titles.size(); ++index) {
                auto path = m_directory.filePath(QString("preview-%1.wav").arg(index));
                writeAudio(path, char(30 + index));
                auto id = peer.call("library.import", {{"path", path}})
                              .value("data")
                              .toObject()
                              .value("song_id")
                              .toInt();
                QVERIFY(id > 0);
                peer.call("song.update_metadata",
                          {{"song_id", id},
                           {"title", titles[index]},
                           {"artist", artists[index]},
                           {"lyrics", "[00:00.00]夜色轻轻落在肩上\n[00:05.00]让旋律陪伴这段时光\n[00:"
                                      "10.00]月光穿过城市的窗\n[00:15.00]此刻只想静静听歌"}});
                peer.call("playlist.add", {{"id", playlistId}, {"song_id", id}});
            }
            peer.call("playlist.play", {{"id", playlistId}});
            QTRY_VERIFY(controllers.playlists->model()->count() >= 2);
            QTRY_VERIFY(controllers.library->songs()->count() >= titles.size());
            if (!homeScreenshot.isEmpty()) {
                for (const QString name : {"月光下的归途 · Midnight walk", "安静的午后与很长很长的歌单名字"}) {
                    const auto id = peer.call("playlist.create", {{"name", name}})
                                        .value("data").toObject().value("playlist_id").toInt();
                    peer.call("playlist.add", {{"id", id}, {"song_id", songId}});
                }
                QTRY_COMPARE(controllers.playlists->model()->count(), 4);
            }
            QTest::qWait(200);
            auto loadedPage = [root](const QString &id) {
                QVariant item;
                QMetaObject::invokeMethod(root, "pageItem", Q_RETURN_ARG(QVariant, item),
                                          Q_ARG(QVariant, QVariant(id)));
                return item.value<QObject *>();
            };
            auto *onlinePage = loadedPage("music_sources");
            QVERIFY(onlinePage);
            auto *online = onlinePage;
            QVERIFY(online);
            QVariantList results;
            for (int index = 0; index < titles.size(); ++index)
                results.append(QVariantMap{{"title", titles[index]},
                                           {"artist", artists[index]},
                                           {"hash", QString("fixture-%1").arg(index)},
                                           {"duration_ms", 210000}});
            online->setProperty("results", results);
            for (const QString language : {"zh", "en"}) {
                translator.setLanguage(language);
                if (!homeScreenshot.isEmpty() && language == "en") {
                    const auto currentId = controllers.playback->song().value("song_id").toInt();
                    const auto title = "A quiet evening beneath the moon, with a very long song title that should stay readable";
                    peer.call("song.update_metadata", {{"song_id", currentId}, {"title", title}});
                    QTRY_COMPARE(controllers.playback->song().value("title").toString(), QString(title));
                }
                for (int width : {1000, 1360}) {
                    root->setProperty("width", width);
                    root->setProperty("height", width == 1000 ? 640 : 860);
                    auto capture = [&](const QString &state) {
                        QTest::mouseMove(window, QPoint(4, 4));
                        QTest::qWait(250);
                        const auto path =
                            screenshot + "-" + language + "-" + QString::number(width) + "-" + state + ".png";
                        QVERIFY(window->grabWindow().save(path));
                    };
                    root->setProperty("currentPlaylist", playlistId);
                    root->setProperty("nowPlayingOpen", false);
                    if (!homeScreenshot.isEmpty()) {
                        root->setProperty("debugEnabled", false);
                        root->setProperty("viewMode", "home");
                        home->setProperty("contentY", 0);
                        capture("home");
                        if (width == 1000) {
                            home->setProperty("contentY", 320);
                            capture("home-scrolled");
                        }
                        continue;
                    }
                    for (const QString page : {"home", "library", "queue", "lyrics", "settings",
                                               "music_sources", "lyrics_debug"}) {
                        root->setProperty("debugEnabled", page == "lyrics_debug");
                        root->setProperty("viewMode", page);
                        capture(page);
                    }
                    root->setProperty("debugEnabled", false);
                    root->setProperty("viewMode", "library");
                    root->setProperty("queueOpen", true);
                    capture("drawer");
                    root->setProperty("queueOpen", false);
                    root->setProperty("currentPlaylist", emptyPlaylistId);
                    root->setProperty("viewMode", "queue");
                    capture("empty-playlist");
                    root->setProperty("currentPlaylist", playlistId);
                    auto *queuePage = loadedPage("queue");
                    QVERIFY(queuePage);
                    auto *panel = queuePage->findChild<QObject *>("queuePanel");
                    QVERIFY(panel);
                    QVERIFY(QMetaObject::invokeMethod(panel, "newPlaylist"));
                    panel->setProperty("operationError", "Could not save playlist. Please retry.");
                    capture("playlist-error");
                    QVERIFY(
                        QMetaObject::invokeMethod(panel->findChild<QObject *>("playlistNamePopup"), "close"));
                    root->setProperty("viewMode", "lyrics");
                    auto *lyricsPage = loadedPage("lyrics");
                    QVERIFY(lyricsPage);
                    auto *lyricsPopup = lyricsPage->findChild<QObject *>("lyricsSearchPopup");
                    QVERIFY(lyricsPopup);
                    QVERIFY(QMetaObject::invokeMethod(lyricsPopup, "openForSong"));
                    capture("lyrics-search");
                    auto *source = lyricsPopup->findChild<QObject *>("lyricsSearchSource");
                    QVERIFY(source);
                    auto *sourcePopup = source->property("popup").value<QObject *>();
                    QVERIFY(sourcePopup);
                    QVERIFY(QMetaObject::invokeMethod(sourcePopup, "open"));
                    capture("lyrics-source");
                    QVERIFY(QMetaObject::invokeMethod(sourcePopup, "close"));
                    QVERIFY(QMetaObject::invokeMethod(lyricsPopup, "close"));
                    QVERIFY(QMetaObject::invokeMethod(editor, "openForSong",
                                                      Q_ARG(QVariant, root->property("song"))));
                    QTRY_VERIFY(editor->property("tagsReady").toBool());
                    capture("metadata");
                    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
                }
            }
            translator.setLanguage("zh");
            root->setProperty("width", 1360);
            root->setProperty("height", 860);
            root->setProperty("viewMode", homeScreenshot.isEmpty() ? "library" : "home");
            home->setProperty("contentY", 0);
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(homeScreenshot.isEmpty() ? screenshot : screenshot + ".png"));
        }
        runtime.stop();
        QTRY_VERIFY(!client.connected());
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Fusion");
    app.setApplicationName("NekoTune Test");
    RefactorTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "refactor_test.moc"
