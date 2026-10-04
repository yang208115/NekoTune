#include "app_paths.h"
#include "application/library/collection_service.h"
#include "application/playback/player_engine.h"
#include "controllers/app_controllers.h"
#include "extensions/extension_view.h"
#include "i18n.h"
#include "runtime/backend_runtime.h"
#include "support/store_fixture.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
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

class ExtensionAudio final : public IPlaybackBackend {
  public:
    QUrl url;
    bool active = false;
    qint64 at = 0;
    void setSource(const QUrl &value) override {
        url = value;
        at = 0;
        active = false;
    }
    QUrl source() const override { return url; }
    void play() override {
        active = true;
        emit stateChanged(PlayerState::Playing);
    }
    void pause() override {
        active = false;
        emit stateChanged(PlayerState::Paused);
    }
    void stop() override {
        active = false;
        at = 0;
        emit stateChanged(PlayerState::Stopped);
    }
    void seek(qint64 value) override { at = value; }
    void setVolume(double) override {}
    qint64 position() const override { return at; }
    qint64 duration() const override { return 8000; }
    double volume() const override { return .8; }
    AudioMetadata metadata() const override { return {}; }
};
class DelayedSource final : public ISourceResolver {
  public:
    QList<Completion> pending;
    QList<QUrl> released;
    void resolve(const SongMetadata &, Completion done) override { pending.append(std::move(done)); }
    void release(const QUrl &url) override {
        if (!url.isEmpty())
            released.append(url);
    }
};
class ExtensionPeer {
  public:
    QLocalSocket socket;
    QByteArray buffer;
    QList<QJsonObject> messages;
    int nextId = 0;
    void read() {
        buffer += socket.readAll();
        while (buffer.contains('\n')) {
            auto end = buffer.indexOf('\n');
            messages.append(QJsonDocument::fromJson(buffer.left(end)).object());
            buffer.remove(0, end + 1);
        }
    }
    QJsonObject call(const QString &method, const QJsonObject &params = {}) {
        const auto id = ++nextId;
        socket.write(QJsonDocument(QJsonObject{{"id", id}, {"method", method}, {"params", params}})
                         .toJson(QJsonDocument::Compact) +
                     '\n');
        socket.flush();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 15'000) {
            QCoreApplication::processEvents();
            read();
            for (int i = 0; i < messages.size(); ++i)
                if (messages[i].value("id").toInt() == id)
                    return messages.takeAt(i);
            socket.waitForReadyRead(10);
        }
        return {{"status", "timeout"}, {"method", method}};
    }
};
class ExtensionsTest final : public QObject {
    Q_OBJECT
    QTemporaryDir m_home;
    SongMetadata remote(const QString &id = "one") {
        SongMetadata song;
        song.providerId = "example.test-source/tones";
        song.providerTrackId = id;
        song.customTitle = "Test tone";
        song.artist = "NekoTune";
        return song;
    }
  private slots:
    void initTestCase() {
        qputenv("NEKOTUNE_HOME", m_home.path().toUtf8());
        qputenv("NEKOTUNE_SOCKET", m_home.filePath("backend.sock").toUtf8());
        qputenv("NEKOTUNE_DB_PATH", m_home.filePath("runtime.sqlite3").toUtf8());
        QVERIFY(AppPaths::prepare());
        QVERIFY(AppPaths::saveSetting("lyrics_offline", true));
    }
    void sourceBrowserUsesOnlyMatchingNavigationPages() {
        IpcClient client;
        ExtensionsController controller(client);
        QJsonObject extension{
            {"id", "test.music"}, {"state", "running"},
            {"registrations", QJsonObject{{"music", QJsonArray{
                QJsonObject{{"id", "test.music/one"}, {"extensionId", "test.music"}},
                QJsonObject{{"id", "test.music/two"}, {"extensionId", "test.music"}, {"page", "browse"}}
            }}}}
        };
        auto update = [&](const QJsonArray &pages) {
            extension.insert("contributes", QJsonObject{
                {"pages", pages},
                {"settings", QJsonArray{QJsonObject{{"id", "test.music/browse"}}}}
            });
            emit client.eventReceived({{"event", "extensions.changed"}, {"extensions", QJsonArray{extension}}});
        };
        const QJsonObject one{{"id", "test.music/one"}};
        const QJsonObject unrelated{{"id", "test.music/help"}};
        update({one, unrelated});
        QCOMPARE(controller.sources().size(), 2);
        QCOMPARE(controller.browserSources().size(), 1);
        QCOMPARE(controller.browserSources().first().toMap().value("id").toString(), QString("test.music/two"));
        update({one, unrelated, QJsonObject{{"id", "test.music/browse"}, {"group", "collection"}}});
        QCOMPARE(controller.browserSources().size(), 1);
        update({one, unrelated, QJsonObject{{"id", "test.music/browse"}, {"group", "utility"}}});
        QCOMPARE(controller.browserSources().size(), 0);
        QCOMPARE(controller.sources().size(), 2);
        update({unrelated});
        QCOMPARE(controller.browserSources().size(), 2);
        emit client.eventReceived({{"event", "extensions.changed"}, {"extensions", QJsonArray{}}});
        QCOMPARE(controller.browserSources().size(), 0);
    }
    void remoteIdentityPersistenceAndAtomicCollections() {
        QTemporaryDir directory;
        StoreFixture store(directory.filePath("songs.db"));
        QVERIFY2(store.isReady(), qPrintable(store.errorString()));
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        ExtensionAudio audio;
        DelayedSource source;
        PlayerEngine player(audio, queue);
        player.setSourceResolver(&source);
        CollectionService collections(store.songRepo, store.queueRepo, store.playlistRepo, store.db,
                                      store.library, queue, player);
        const int playlist = store.createPlaylist("Remote");
        QVERIFY(playlist);
        auto first = collections.enqueueRemote(remote(), false);
        QVERIFY(first);
        auto duplicate = collections.enqueueRemote(remote(), false);
        QVERIFY(duplicate);
        QVERIFY(first.value() != duplicate.value());
        QCOMPARE(store.songs().size(), 1);
        QCOMPARE(store.library.snapshot().songs.size(), 0);
        QVERIFY(collections.addRemoteToPlaylist(playlist, remote()));
        QCOMPARE(store.playlists().first().items.size(), 1);
        QVERIFY(store.songs().first().hash.isEmpty());
        QVERIFY(store.songs().first().resourceKey().startsWith("remote-"));
        QueueService restored(store.queueRepo, store.songRepo, store.db);
        QCOMPARE(restored.queue().size(), 2);
        QSqlQuery query(store.db.database());
        QVERIFY(query.exec("CREATE TRIGGER reject_queue BEFORE INSERT ON queue_items BEGIN SELECT "
                           "RAISE(ABORT,'fixture'); END"));
        auto failed = collections.enqueueRemote(remote("two"), true);
        QVERIFY(!failed);
        QCOMPARE(store.songs().size(), 1);
        QCOMPARE(queue.queue().size(), 2);
        QVERIFY(!audio.active);
        QVERIFY(query.exec("DROP TRIGGER reject_queue"));
        QVERIFY(collections.playPlaylist(playlist));
        QCOMPARE(source.pending.size(), 1);
        player.shutdown();
    }
    void delayedResolutionRespectsPauseStopAndSelection() {
        QTemporaryDir directory;
        StoreFixture store(directory.filePath("songs.db"));
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        ExtensionAudio audio;
        DelayedSource source;
        PlayerEngine player(audio, queue);
        player.setSourceResolver(&source);
        CollectionService collections(store.songRepo, store.queueRepo, store.playlistRepo, store.db,
                                      store.library, queue, player);
        QVERIFY(collections.enqueueRemote(remote(), true));
        QCOMPARE(source.pending.size(), 1);
        QVERIFY(player.pause());
        source.pending.takeFirst()(QUrl("http://127.0.0.1/paused"));
        QVERIFY(!audio.active);
        QVERIFY(player.play());
        QVERIFY(audio.active);
        QVERIFY(collections.enqueueRemote(remote("two"), true));
        QVERIFY(player.stop());
        source.pending.takeFirst()(QUrl("http://127.0.0.1/stale"));
        QVERIFY(audio.source().isEmpty());
        QVERIFY(!audio.active);
        QVERIFY(source.released.contains(QUrl("http://127.0.0.1/stale")));
        QVERIFY(player.play());
        QVERIFY(player.previous());
        QCOMPARE(source.pending.size(), 2);
        source.pending.takeFirst()(QUrl("http://127.0.0.1/older"));
        QVERIFY(audio.source().isEmpty());
        source.pending.takeFirst()(QUrl("http://127.0.0.1/current"));
        QVERIFY(audio.active);
        emit audio.failed("temporary network failure");
        QCOMPARE(queue.queue().size(), 2);
        QVERIFY(player.snapshot().state == PlayerState::Error);
        QVERIFY(player.play());
        source.pending.takeFirst()(QUrl("http://127.0.0.1/retry"));
        QVERIFY(audio.active);
        player.sourcesReloading({"example.test-source"}, true);
        QVERIFY(audio.source().isEmpty());
        QVERIFY(player.pause());
        player.sourcesReloading({"example.test-source"}, false);
        QCOMPARE(source.pending.size(), 1);
        source.pending.takeFirst()(QUrl("http://127.0.0.1/reloaded-paused"));
        QVERIFY(!audio.active);
        QVERIFY(player.play());
        player.sourcesReloading({"example.test-source"}, true);
        QVERIFY(player.stop());
        player.sourcesReloading({"example.test-source"}, false);
        QCOMPARE(source.pending.size(), 0);
        QVERIFY(!audio.active);
        QVERIFY(player.play());
        source.pending.takeFirst()(QUrl("http://127.0.0.1/restarted"));
        player.sourceUnavailable(remote().providerId);
        QCOMPARE(queue.queue().size(), 2);
        QVERIFY(audio.source().isEmpty());
        player.shutdown();
    }
    void mixedQueuePlaybackModes_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("sequential") << int(PlaybackMode::Sequential);
        QTest::newRow("repeat-one") << int(PlaybackMode::RepeatOne);
        QTest::newRow("repeat-all") << int(PlaybackMode::RepeatAll);
        QTest::newRow("shuffle") << int(PlaybackMode::Shuffle);
    }
    void mixedQueuePlaybackModes() {
        QFETCH(int, mode);
        QTemporaryDir directory;
        StoreFixture store(directory.filePath("mixed.db"));
        const auto local = store.getOrCreateSong("local-audio", "/music/local.wav");
        const auto online = store.songRepo.getOrCreateRemote(remote());
        QVERIFY(local);
        QVERIFY(online);
        PlayerQueue sequence;
        sequence.add(local->firstPath, *local);
        sequence.add({}, online.value());
        sequence.add({}, online.value());
        sequence.setCurrentIndex(0);
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        ExtensionAudio audio;
        DelayedSource source;
        PlayerEngine player(audio, queue);
        player.setSourceResolver(&source);
        QVERIFY(player.setPlaybackMode(static_cast<PlaybackMode>(mode)));
        QVERIFY(player.replaceQueue(sequence));
        const auto localId = player.snapshot().song->id;
        QVERIFY(audio.url.isLocalFile());
        QVERIFY(player.next());
        const auto onlineId = player.snapshot().song->id;
        QVERIFY(onlineId != localId);
        QVERIFY(player.snapshot().song->metadata.isRemote());
        QCOMPARE(source.pending.size(), 1);
        source.pending.takeFirst()(QUrl("http://127.0.0.1/mixed"));
        QVERIFY(audio.active);
        emit audio.ended();
        if (mode == int(PlaybackMode::RepeatOne))
            QCOMPARE(player.snapshot().song->id, onlineId);
        else
            QVERIFY(player.snapshot().song->id != onlineId);
        while (!source.pending.isEmpty())
            source.pending.takeFirst()(QUrl("http://127.0.0.1/next"));
        const auto beforePrevious = player.snapshot().song->id;
        QVERIFY(player.previous());
        QVERIFY(player.snapshot().song->id != beforePrevious);
        QCOMPARE(queue.queue().size(), 3);
        player.shutdown();
    }
    void migrationPreservesLegacyColumnsAndRollsBack_data() {
        QTest::addColumn<bool>("autoIncrement");
        QTest::newRow("autoincrement") << true;
        QTest::newRow("legacy-primary-key") << false;
    }
    void migrationPreservesLegacyColumnsAndRollsBack() {
        QFETCH(bool, autoIncrement);
        QTemporaryDir directory;
        const auto path = directory.filePath("legacy.db");
        {
            auto database = QSqlDatabase::addDatabase("QSQLITE", "legacy-extension-fixture");
            database.setDatabaseName(path);
            QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec(
                QString("CREATE TABLE songs (id INTEGER PRIMARY KEY %1,hash TEXT %2,"
                        "first_path TEXT NOT NULL,custom_title TEXT NOT NULL DEFAULT '',artist "
                        "TEXT NOT NULL DEFAULT '',lyrics TEXT NOT NULL DEFAULT '',created_at TEXT DEFAULT "
                        "CURRENT_TIMESTAMP,updated_at TEXT DEFAULT CURRENT_TIMESTAMP,legacy_note TEXT)")
                    .arg(autoIncrement ? "AUTOINCREMENT" : "",
                         autoIncrement ? "NOT NULL UNIQUE" : "UNIQUE NOT NULL")));
            QVERIFY(query.exec("INSERT INTO songs(id,hash,first_path,legacy_note) "
                               "VALUES(42,'legacy-hash','/old/song.wav','preserve me')"));
            QVERIFY(query.exec("CREATE INDEX legacy_title ON songs(custom_title)"));
            QVERIFY(query.exec("CREATE TABLE songs_extension_migration (blocker INTEGER)"));
        }
        QSqlDatabase::removeDatabase("legacy-extension-fixture");
        {
            DatabaseSession failed(path);
            QVERIFY(!failed.isReady());
        }
        {
            auto database = QSqlDatabase::addDatabase("QSQLITE", "legacy-extension-check");
            database.setDatabaseName(path);
            QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec("SELECT hash,legacy_note FROM songs WHERE id=42"));
            QVERIFY(query.next());
            QCOMPARE(query.value(1).toString(), "preserve me");
            query.finish();
            QVERIFY(query.exec("DROP TABLE songs_extension_migration"));
        }
        QSqlDatabase::removeDatabase("legacy-extension-check");
        StoreFixture migrated(path);
        QVERIFY2(migrated.isReady(), qPrintable(migrated.errorString()));
        QVERIFY(QFileInfo::exists(path + ".pre-extensions"));
        QCOMPARE(migrated.songById(42)->hash, "legacy-hash");
        QSqlQuery check(migrated.db.database());
        QVERIFY(check.exec("SELECT legacy_note FROM songs WHERE id=42"));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toString(), "preserve me");
        QVERIFY(migrated.songRepo.getOrCreateRemote(remote()));
        QVERIFY(check.exec("PRAGMA foreign_key_check"));
        QVERIFY(!check.next());
    }
    void backendExtensionsAndMusicRoundTrip() {
        BackendRuntime runtime;
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        ExtensionPeer peer;
        peer.socket.connectToServer(runtime.serverName());
        QVERIFY(peer.socket.waitForConnected(3000));
        QTRY_VERIFY_WITH_TIMEOUT(
            peer.call("extensions.list").value("data").toObject().value("runtimeReady").toBool(), 15'000);
        auto install =
            peer.call("extensions.install",
                      {{"path", QStringLiteral(NEKOTUNE_SOURCE_DIR "/extensions/examples/test-source")},
                       {"development", true}});
        QCOMPARE(install.value("status").toString(), "ok");
        auto enabled = peer.call("extensions.enable", {{"id", "example.test-source"}, {"trusted", true}});
        QVERIFY2(enabled.value("status") == "ok",
                 qPrintable(QString::fromUtf8(QJsonDocument(enabled).toJson())));
        auto sources = peer.call("music.sources").value("data").toObject().value("sources").toArray();
        QCOMPARE(sources.size(), 1);
        const QString provider = "example.test-source/tones";
        const auto tracks = peer.call("music.search", {{"source", provider}, {"query", "220"}})
                                .value("data")
                                .toObject()
                                .value("tracks")
                                .toArray();
        QCOMPARE(tracks.size(), 1);
        const auto track = tracks.first().toObject();
        auto queued = peer.call("music.enqueue", {{"source", provider}, {"track", track}});
        QCOMPARE(queued.value("status").toString(), "ok");
        ExtensionPeer second;
        second.socket.connectToServer(runtime.serverName());
        QVERIFY(second.socket.waitForConnected(3000));
        QCOMPARE(second.call("music.sources").value("data").toObject().value("sources").toArray().size(), 1);
        const auto create = peer.call("playlist.create", {{"name", "Extension playlist"}});
        const int playlist = create.value("data").toObject().value("playlist_id").toInt();
        QVERIFY2(playlist > 0, qPrintable(QString::fromUtf8(QJsonDocument(create).toJson())));
        auto added = peer.call("music.add_to_playlist",
                               {{"source", provider}, {"track", track}, {"playlist_id", playlist}});
        QCOMPARE(added.value("status").toString(), "ok");
        auto download = peer.call("music.download", {{"source", provider}, {"track", track}});
        QCOMPARE(download.value("status").toString(), "ok");
        QTRY_VERIFY_WITH_TIMEOUT(peer.call("library.list")
                                         .value("data")
                                         .toObject()
                                         .value("library")
                                         .toObject()
                                         .value("songs")
                                         .toArray()
                                         .size() > 0,
                                 15'000);
        QCOMPARE(peer.call("queue.status").value("data").toObject().value("items").toArray().size(), 1);
        const auto secondDownload = peer.call("music.download", {{"source", provider}, {"track", track}});
        const auto downloadTask = secondDownload.value("data").toObject().value("task").toString();
        QVERIFY(!downloadTask.isEmpty());
        auto finished = [&] {
            peer.read();
            return std::any_of(peer.messages.cbegin(), peer.messages.cend(), [&](const QJsonObject &message) {
                return message.value("event") == "music.download" && message.value("task") == downloadTask &&
                       message.value("state") == "finished";
            });
        };
        QTRY_VERIFY_WITH_TIMEOUT(finished(), 15'000);
        QCOMPARE(peer.call("library.list")
                     .value("data")
                     .toObject()
                     .value("library")
                     .toObject()
                     .value("songs")
                     .toArray()
                     .size(),
                 1);
        QCOMPARE(peer.call("extensions.disable", {{"id", "example.test-source"}}).value("status").toString(),
                 "ok");
        QCOMPARE(peer.call("queue.status").value("data").toObject().value("items").toArray().size(), 1);
        second.socket.abort();
        peer.socket.abort();
        runtime.stop();
    }
    void isolatedQmlViewsReloadAndRecover() {
        IpcClient client;
        AppControllers controllers(client);
        I18n translator;
        QQuickWindow window;
        window.resize(1000, 700);
        window.show();
        ExtensionView view(window.contentItem());
        view.setSize(window.size());
        view.controllers = &controllers;
        view.translator = &translator;
        view.hostWindow = &window;
        // A component created by QQmlComponent exercises the real QML completion lifecycle.
        QQmlEngine engine;
        QQmlComponent component(&engine);
        engine.rootContext()->setContextProperty("testApp", &controllers);
        engine.rootContext()->setContextProperty("testI18n", &translator);
        component.setData("import QtQuick; import NekoTune 1.0; ExtensionView { width:1000; height:700; "
                          "controllers:testApp; translator:testI18n }",
                          QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *item = qobject_cast<ExtensionView *>(object.data());
        QVERIFY(item);
        item->setParentItem(window.contentItem());
        item->setDescriptor(
            {{"source", QStringLiteral(NEKOTUNE_SOURCE_DIR "/extensions/examples/custom-ui/Shell.qml")},
             {"extensionId", "example.custom-ui"},
             {"generation", 1}});
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 5000);
        auto *first = item->item();
        QPointer<QQuickItem> old(first);
        item->setDescriptor(
            {{"source", QStringLiteral(NEKOTUNE_SOURCE_DIR "/extensions/examples/custom-ui/Shell.qml")},
             {"extensionId", "example.custom-ui"},
             {"generation", 2}});
        QTRY_VERIFY(old.isNull());
        QTRY_VERIFY(item->ready());
        if (const auto directory = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT_DIR");
            !directory.isEmpty()) {
            QTest::qWait(150);
            QVERIFY(window.grabWindow().save(directory + "/extension-shell.png"));
        }
        item->setDescriptor({{"source", "/nonexistent.qml"}});
        QTRY_VERIFY(!item->ready());
        QVERIFY(!item->error().isEmpty());
        item->setDescriptor({});
        QTRY_VERIFY(item->error().isEmpty());
    }
    void managementThemeAndShellInRealFrontend() {
        BackendRuntime runtime;
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        ExtensionPeer peer;
        peer.socket.connectToServer(runtime.serverName());
        QVERIFY(peer.socket.waitForConnected(3000));
        QTRY_VERIFY_WITH_TIMEOUT(
            peer.call("extensions.list").value("data").toObject().value("runtimeReady").toBool(), 15'000);
        for (const QString name : {"test-source", "custom-ui"}) {
            const QString id = "example." + name;
            auto result =
                peer.call("extensions.install",
                          {{"path", QStringLiteral(NEKOTUNE_SOURCE_DIR "/extensions/examples/") + name},
                           {"development", true},
                           {"replace", true}});
            QCOMPARE(result.value("status").toString(), "ok");
            QCOMPARE(
                peer.call("extensions.enable", {{"id", id}, {"trusted", true}}).value("status").toString(),
                "ok");
        }
        IpcClient client;
        AppControllers controllers(client);
        I18n translator;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("controllers", &controllers);
        engine.rootContext()->setContextProperty("ipcClient", &client);
        engine.rootContext()->setContextProperty("i18n", &translator);
        engine.rootContext()->setContextProperty("lyricsDebugEnabled", false);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(1000, 720);
        client.connectBackend();
        QTRY_VERIFY_WITH_TIMEOUT(controllers.extensions->items().size() >= 2, 5000);
        QCOMPARE(controllers.extensions->menuActions("song", "zh").size(), 1);
        window->setProperty("viewMode", "extensions");
        auto pageItem = [window](const QString &id) -> QObject * {
            QVariant result;
            QMetaObject::invokeMethod(window, "pageItem", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, id));
            return result.value<QObject *>();
        };
        QTRY_VERIFY(pageItem("extensions"));
        QVERIFY(pageItem("extensions")->findChild<QQuickItem *>("extensionsPanel"));
        const auto screenshots = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT_DIR");
        auto capture = [&](const QString &name) {
            QTest::qWait(180);
            return screenshots.isEmpty() || window->grabWindow().save(screenshots + "/" + name + ".png");
        };
        translator.setLanguage("zh");
        QVERIFY(capture("extension-manager-zh"));
        translator.setLanguage("en");
        QVERIFY(capture("extension-manager-en"));
        window->setProperty("viewMode", "music_sources");
        QTRY_VERIFY(pageItem("music_sources"));
        QVERIFY(QMetaObject::invokeMethod(pageItem("music_sources"), "search", Q_ARG(QVariant, false)));
        QTRY_VERIFY(
            engine.newQObject(pageItem("music_sources")).property("tracks").property("length").toInt() > 0);
        QVERIFY(capture("music-sources"));
        const auto installed = peer.call("extensions.install",
            {{"path", QStringLiteral(NEKOTUNE_SOURCE_DIR "/tests/fixtures/dedicated-source")},
             {"development", true}, {"replace", true}});
        QCOMPARE(installed.value("status").toString(), QString("ok"));
        QCOMPARE(peer.call("extensions.enable", {{"id", "test.dedicated-source"}, {"trusted", true}})
                     .value("status").toString(), "ok");
        QTRY_COMPARE(controllers.extensions->sources().size(), 2);
        QTRY_COMPARE(controllers.extensions->browserSources().size(), 1);
        QCOMPARE(controllers.extensions->browserSources().first().toMap().value("id").toString(),
                 QString("example.test-source/tones"));
        auto *picker = pageItem("music_sources")->findChild<QObject *>("musicSourcePicker");
        QVERIFY(picker);
        QTRY_COMPARE(picker->property("count").toInt(), 1);
        QTRY_COMPARE(picker->property("currentValue").toString(), QString("example.test-source/tones"));
        QVERIFY(capture("music-sources-mixed"));
        QCOMPARE(peer.call("extensions.disable", {{"id", "example.test-source"}}).value("status").toString(), "ok");
        QTRY_VERIFY(controllers.extensions->browserSources().isEmpty());
        QTRY_COMPARE(window->property("viewMode").toString(), QString("home"));
        QVERIFY(!visualItem(window->contentItem(), "nav_music_sources"));
        translator.setLanguage("zh");
        QVERIFY(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, "test.dedicated-source/music"),
                                         Q_ARG(QVariant, QVariant())));
        QVERIFY(capture("music-dedicated-only"));
        QCOMPARE(peer.call("extensions.enable", {{"id", "example.test-source"}}).value("status").toString(), "ok");
        QTRY_COMPARE(controllers.extensions->browserSources().size(), 1);
        QTRY_VERIFY(visualItem(window->contentItem(), "nav_music_sources"));
        QCOMPARE(peer.call("extensions.reload", {{"id", "example.test-source"}}).value("status").toString(), "ok");
        QTRY_COMPARE(controllers.extensions->browserSources().size(), 1);
        QTRY_VERIFY(visualItem(window->contentItem(), "nav_music_sources"));
        QCOMPARE(
            peer.call("extensions.select", {{"slot", "theme"}, {"contribution", "example.custom-ui/ocean"}})
                .value("status")
                .toString(),
            "ok");
        QTRY_COMPARE(controllers.extensions->themeTokens().value("bgCanvas").toString(), "#091923");
        QTRY_COMPARE(window->color(), QColor("#091923"));
        QQmlApplicationEngine recovery;
        recovery.rootContext()->setContextProperty("controllers", &controllers);
        recovery.rootContext()->setContextProperty("i18n", &translator);
        recovery.rootContext()->setContextProperty("ignoreExtensionTheme", true);
        recovery.load(QUrl("qrc:/qml/ExtensionsWindow.qml"));
        QVERIFY(!recovery.rootObjects().isEmpty());
        auto *recoveryWindow = qobject_cast<QQuickWindow *>(recovery.rootObjects().first());
        QVERIFY(recoveryWindow);
        recoveryWindow->resize(760, 580);
        recoveryWindow->show();
        QCOMPARE(recoveryWindow->color(), QColor("#0E0D14"));
        if (!screenshots.isEmpty()) {
            QTest::qWait(180);
            QVERIFY(recoveryWindow->grabWindow().save(screenshots + "/extension-recovery-narrow.png"));
        }
        recoveryWindow->close();
        QCOMPARE(
            peer.call("extensions.select", {{"slot", "shell"}, {"contribution", "example.custom-ui/shell"}})
                .value("status")
                .toString(),
            "ok");
        auto *view = window->findChild<ExtensionView *>("shellExtension");
        QVERIFY(view);
        QTRY_VERIFY2(view->ready(), qPrintable(view->error()));
        QVERIFY(capture("extension-shell-integrated"));
        QCOMPARE(peer.call("extensions.disable", {{"id", "example.custom-ui"}}).value("status").toString(),
                 "ok");
        QTRY_VERIFY(!view->ready());
        QTRY_COMPARE(window->color(), QColor("#0E0D14"));
        window->close();
        peer.socket.abort();
        runtime.stop();
    }
};
QTEST_MAIN(ExtensionsTest)
#include "extensions_test.moc"
