#include "application/lyrics_service.h"
#include "controllers/app_controllers.h"
#include "core/player_engine.h"
#include "i18n.h"
#include "infrastructure/import_executor.h"
#include "infrastructure/lyrics_storage.h"
#include "ipc/ipc_router.h"
#include "ipc/serialization.h"
#include "runtime/backend_runtime.h"
#include "support/store_fixture.h"
#include <QDataStream>
#include <QFile>
#include <QJSValue>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace nekotune;

class FakeAudio final : public IPlaybackBackend {
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
    void writeAudio(const QString &path, char sample) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        constexpr quint32 size = 8000 * 2 * 30;
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
        qputenv("XDG_CONFIG_HOME", m_directory.filePath("config").toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath("data").toUtf8());
        qputenv("XDG_CACHE_HOME", m_directory.filePath("cache").toUtf8());
        qunsetenv("KUGOU_ACCOUNT_API_KEY");
        qunsetenv("KUGOU_ACCOUNT_API_KEY_FILE");
        writeAudio(m_audio, 0);
    }
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
    void socketCompatibilityAndConnectedShutdown() {
        BackendRuntime runtime;
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
        QCOMPARE(sources.size(), 2);
        runtime.stop(); // Live socket must not call into a destroyed client-buffer map.
        QVERIFY(runtime.start());
        RpcPeer reconnected;
        QVERIFY(reconnected.connect(m_socket));
        QVERIFY(peer.socket.waitForDisconnected(50) || peer.socket.state() == QLocalSocket::UnconnectedState);
        QCOMPARE(reconnected.call("player.status").value("status").toString(), QString("ok"));
        runtime.stop();
    }
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
    void orderedImportsAndDisconnect() {
        BackendRuntime runtime;
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
            QCOMPARE(queue[index].toObject().value("path").toString(), paths[index]);
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
    void realQmlMetadataSelectionAndMute() {
        BackendRuntime runtime;
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
        controllers.library->toggleSelection(songId);
        QCOMPARE(controllers.library->selectedSongIds().size(), 1);
        peer.call("player.play", {{"path", m_audio}});
        QTest::qWait(300);
        QCOMPARE(controllers.library->selectedSongIds().size(), 1);
        controllers.playback->setVolume(0);
        QTRY_COMPARE(controllers.playback->volume(), 0.0);
        QCOMPARE(root->property("volume").toDouble(), 0.0);
        controllers.playback->toggleMute();
        QTRY_VERIFY(controllers.playback->volume() > 0);
        auto *editor = root->findChild<QObject *>("metadataEditor");
        QVERIFY(editor);
        const QVariant record = QVariantMap{{"song_id", songId}, {"title", "Incomplete list row"}};
        QVERIFY(QMetaObject::invokeMethod(editor, "openForSong", Q_ARG(QVariant, record)));
        QTRY_VERIFY(editor->property("tagsReady").toBool());
        auto *title = editor->findChild<QObject *>("metadataTitleField");
        auto *lyrics = editor->findChild<QObject *>("metadataLyricsField");
        QVERIFY(title && lyrics);
        QCOMPARE(lyrics->property("text").toString(), QString("Keep my custom lyrics"));
        title->setProperty("text", "Edited through QML");
        QVERIFY(QMetaObject::invokeMethod(editor, "save"));
        QTRY_VERIFY(!editor->property("visible").toBool());
        auto metadata = peer.call("song.metadata", {{"song_id", songId}}).value("data").toObject();
        QCOMPARE(metadata.value("custom_title").toString(), QString("Edited through QML"));
        QCOMPARE(metadata.value("lyrics").toString(), QString("Keep my custom lyrics"));
        for (const QString page : {"queue", "library", "lyrics", "kugou", "settings", "lyrics_debug"}) {
            root->setProperty("viewMode", page);
            QTest::qWait(30);
            QVariant status;
            QVERIFY(QMetaObject::invokeMethod(root, "pageStatus", Q_RETURN_ARG(QVariant, status),
                                              Q_ARG(QVariant, QVariant(page))));
            QCOMPARE(status.toInt(), 1);
        }
        // Registration alone is enough to expose a new page; Main.qml has no feature branch for it.
        const QString extraQml =
            "import QtQuick; Item { required property var shell; required property var controllers; required "
            "property var transport; required property var translator; signal importRequested(int id); "
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
        QCOMPARE(extraStatus.toInt(), 1);
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
        const auto screenshot = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            auto *window = qobject_cast<QQuickWindow *>(root);
            QVERIFY(window);
            for (const QString page : {"queue", "library", "lyrics", "settings", "kugou", "lyrics_debug"}) {
                root->setProperty("viewMode", page);
                QTest::qWait(100);
                QVERIFY(window->grabWindow().save(screenshot + "-" + page + ".png"));
            }
            root->setProperty("viewMode", "library");
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(screenshot));
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
