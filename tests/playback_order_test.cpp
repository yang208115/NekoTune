#include "support/audio_backend.h"
#include "application/playback/playback_order_service.h"
#include "app_paths.h"
#include "controllers/playback_controller.h"
#include "application/playback/player_engine.h"
#include "runtime/backend_runtime.h"
#include "support/store_fixture.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace nekotune;

class OrderAudio final : public TestAudioBackend {
  public:
    QUrl path;
    qint64 clock = 0;
    bool playing = false;
    int loads = 0;
    void setSource(const QUrl &value) override { path = value; clock = 0; ++loads; }
    QUrl source() const override { return path; }
    void play() override { playing = true; emit stateChanged(PlayerState::Playing); }
    void pause() override { playing = false; emit stateChanged(PlayerState::Paused); }
    void stop() override { playing = false; clock = 0; emit stateChanged(PlayerState::Stopped); }
    void seek(qint64 value) override { clock = value; }
    void setVolume(double) override {}
    qint64 position() const override { return clock; }
    qint64 duration() const override { return 30000; }
    double volume() const override { return .8; }
    AudioMetadata metadata() const override { return {}; }
};

class OrderPeer {
  public:
    QLocalSocket socket;
    QByteArray buffer;
    QList<QJsonObject> messages;
    int nextId = 1;
    bool connect(const QString &path) {
        socket.connectToServer(path);
        return socket.waitForConnected(3000);
    }
    void read() {
        buffer += socket.readAll();
        while (buffer.contains('\n')) {
            const int end = buffer.indexOf('\n');
            messages.append(QJsonDocument::fromJson(buffer.left(end)).object());
            buffer.remove(0, end + 1);
        }
    }
    QJsonObject call(const QString &method, const QJsonObject &params = {}) {
        const int id = nextId++;
        socket.write(QJsonDocument(QJsonObject{{"id", id}, {"method", method}, {"params", params}})
                         .toJson(QJsonDocument::Compact) + "\n");
        socket.flush();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            read();
            for (int i = 0; i < messages.size(); ++i)
                if (messages.at(i).value("id").toInt() == id)
                    return messages.takeAt(i);
            socket.waitForReadyRead(20);
        }
        return {};
    }
    QJsonObject event(const QString &name) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 3000) {
            read();
            for (int i = 0; i < messages.size(); ++i)
                if (messages.at(i).value("event").toString() == name)
                    return messages.takeAt(i);
            socket.waitForReadyRead(20);
        }
        return {};
    }
};

class PlaybackOrderTest final : public QObject {
    Q_OBJECT
    QTemporaryDir m_directory;
    PlayerQueue queue(int count = 4) {
        PlayerQueue result;
        for (int i = 0; i < count; ++i)
            result.add(QString("/music/%1.wav").arg(i), {1, "same-song"});
        if (count) result.setCurrentIndex(0);
        return result;
    }
    PlayerQueue storedQueue(StoreFixture &store, int count = 4) {
        const auto song = store.getOrCreateSong("same-song", "/music/test.wav");
        PlayerQueue result;
        if (!song) return result;
        for (int i = 0; i < count; ++i)
            result.add(QString("/music/%1.wav").arg(i), *song);
        if (count) result.setCurrentIndex(0);
        return result;
    }
    int selected(const PlayerEngine &player) { return player.snapshot().song->id; }
    int advance(PlaybackOrderService &order, PlayerQueue &queue, PlaybackAdvance reason = PlaybackAdvance::Next) {
        auto choice = order.propose(queue, reason);
        const int id = choice.queueId;
        if (id) queue.setCurrentIndex(queue.indexById(id));
        order.confirm(std::move(choice));
        return id;
    }
  private slots:
    void initTestCase() {
        QVERIFY(m_directory.isValid());
        qputenv("NEKOTUNE_HOME", m_directory.filePath("music").toUtf8());
        qputenv("NEKOTUNE_DB_PATH", m_directory.filePath("runtime.sqlite3").toUtf8());
        qputenv("NEKOTUNE_SOCKET", m_directory.filePath("runtime.sock").toUtf8());
        QVERIFY(AppPaths::prepare());
        QVERIFY(AppPaths::saveSetting("lyrics_offline", true));
    }
    void sequentialAndRepeatControls_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("sequential") << int(PlaybackMode::Sequential);
        QTest::newRow("repeat-one") << int(PlaybackMode::RepeatOne);
        QTest::newRow("repeat-all") << int(PlaybackMode::RepeatAll);
    }
    // Mode changes must preserve the current source load and playback clock.
    // Natural completion is distinct from explicit Next/Previous navigation.
    // Repeat-one repeats only the natural end of the selected occurrence.
    // Repeat-all wraps both ends while sequential stops at the tail.
    // Previous always navigates; it does not become a restart-after-three-seconds action.
    // The fake decoder records loads so an accidental reload is observable.
    void sequentialAndRepeatControls() {
        QFETCH(int, mode);
        const auto playbackMode = PlaybackMode(mode);
        QTemporaryDir dir;
        StoreFixture store(dir.filePath("songs.sqlite3"));
        QueueService queues(store.queueRepo, store.songRepo, store.db);
        OrderAudio audio;
        PlayerEngine player(audio, queues);
        auto list = storedQueue(store, 3);
        QVERIFY(player.replaceQueue(list));
        audio.seek(12000);
        const int loads = audio.loads;
        QVERIFY(player.setPlaybackMode(playbackMode));
        QCOMPARE(audio.clock, 12000);
        QCOMPARE(audio.loads, loads);
        QVERIFY(player.previous());
        QCOMPARE(selected(player), playbackMode == PlaybackMode::RepeatAll ? 3 : 1);
        QVERIFY(player.playItem(2));
        audio.seek(20000);
        QVERIFY(player.previous());
        QCOMPARE(selected(player), 1); // Always changes track even after three seconds.
        QVERIFY(player.playItem(2));
        emit audio.ended();
        QCOMPARE(selected(player), playbackMode == PlaybackMode::RepeatOne ? 2 : 3);
        QVERIFY(player.playItem(3));
        emit audio.ended();
        QCOMPARE(audio.playing, playbackMode != PlaybackMode::Sequential);
        QCOMPARE(selected(player), playbackMode == PlaybackMode::RepeatAll ? 1 : 3);
        QVERIFY(player.playItem(3));
        QVERIFY(player.next());
        QCOMPARE(audio.playing, playbackMode == PlaybackMode::RepeatAll);
        QVERIFY(player.playItem(1));
        QVERIFY(player.next());
        QCOMPARE(selected(player), 2); // Manual Next never repeats in repeat-one.
    }
    // Each occurrence must be drawn once per shuffle bag, including repeated songs.
    // A cycle boundary must not immediately repeat its last entry when alternatives exist.
    // Previous walks actual history and Next retraces that branch before drawing again.
    // Metadata synchronization during history navigation must not consume randomness.
    // Repeated unconfirmed proposals must remain identical, including a new bag refill.
    // The persisted queue order stays unchanged even while playback order is random.
    void shuffleRoundsAndHistory() {
        auto list = queue();
        const auto original = list.records();
        PlaybackOrderService order(std::make_unique<ShuffleBagStrategy>(42), PlaybackMode::Shuffle);
        order.reset(list);
        QSet<int> heard{1};
        QList<int> sequence{1};
        for (int i = 0; i < 3; ++i) {
            const int id = advance(order, list, PlaybackAdvance::Ended);
            QVERIFY(!heard.contains(id));
            heard.insert(id); sequence.append(id);
        }
        QCOMPARE(heard.size(), 4); // Repeated song metadata still represents distinct queue items.
        const int last = sequence.last();
        const int next = advance(order, list);
        QVERIFY(next != last);
        heard = {next};
        QCOMPARE(advance(order, list, PlaybackAdvance::Previous), last);
        order.syncQueue(list); // Metadata updates while revisiting the last round must not consume this round.
        QCOMPARE(advance(order, list), next);
        for (int i = 0; i < 3; ++i) {
            const int id = advance(order, list);
            QVERIFY(!heard.contains(id));
            heard.insert(id);
        }
        QCOMPARE(heard.size(), 4);
        const int current = list.at(list.currentIndex()).id;
        const int previous = advance(order, list, PlaybackAdvance::Previous);
        QVERIFY(previous != current);
        QCOMPARE(advance(order, list), current);
        for (int i = 0; i < list.size(); ++i) QCOMPARE(list.records().at(i).path, original.at(i).path);
        // Proposals, including a new round's shuffle, are repeatable until confirmed.
        const int proposed = order.propose(list, PlaybackAdvance::Next).queueId;
        for (int i = 0; i < 10; ++i) QCOMPARE(order.propose(list, PlaybackAdvance::Next).queueId, proposed);
        QCOMPARE(advance(order, list), proposed);
    }
    // Removing a pending occurrence must take it out of future bag draws.
    // Appending an occurrence joins the remaining cycle without replaying already heard items.
    // Metadata-only edits must leave the next proposal unchanged.
    // Explicit selection resets history around the newly chosen occurrence.
    // Empty and single-item queues exercise the no-alternative boundaries.
    // Use a fixed seed to verify policy rather than random luck.
    void editsAndReset() {
        auto list = queue();
        PlaybackOrderService order(std::make_unique<ShuffleBagStrategy>(7), PlaybackMode::Shuffle);
        order.reset(list);
        const int first = advance(order, list);
        const int doomed = order.propose(list, PlaybackAdvance::Next).queueId;
        list.removeAt(list.indexById(doomed));
        const int added = list.add("/music/added.wav", {1, "same-song"});
        order.syncQueue(list);
        QSet<int> pending;
        for (int i = 0; i < 2; ++i) pending.insert(advance(order, list));
        QVERIFY(pending.contains(added));
        QVERIFY(!pending.contains(doomed));
        QVERIFY(!pending.contains(first));
        const int proposal = order.propose(list, PlaybackAdvance::Next).queueId;
        list.updateSongMetadata({1, "same-song", {}, "Renamed"});
        order.syncQueue(list);
        QCOMPARE(order.propose(list, PlaybackAdvance::Next).queueId, proposal);
        list.setCurrentIndex(list.indexById(added));
        order.reset(list); // Explicit item selection resets both history and the current round.
        QCOMPARE(advance(order, list, PlaybackAdvance::Previous), added);
        QVERIFY(advance(order, list) != added);
        list.clear(); order.reset(list);
        QCOMPARE(order.propose(list, PlaybackAdvance::Next).queueId, 0);
        list = queue(1); order.reset(list);
        QCOMPARE(advance(order, list), 1);
        QCOMPARE(advance(order, list, PlaybackAdvance::Previous), 1);
    }
    // Inject failure at queue replacement after a shuffle proposal is computed.
    // Repeated failed Next calls must leave source, progress and history unchanged.
    // Natural completion failure also emits one application error without confirming a draw.
    // After removing the trigger, the first success must choose the original proposal.
    // Pause/resume must keep its identity while Previous/Next retrace confirmed history.
    // An empty natural-end notification is harmless, but manual navigation remains invalid.
    void storageFailureDoesNotAdvanceShuffle() {
        QTemporaryDir dir;
        StoreFixture store(dir.filePath("songs.sqlite3"));
        QueueService queues(store.queueRepo, store.songRepo, store.db);
        OrderAudio audio;
        PlayerEngine player(audio, queues, std::make_unique<ShuffleBagStrategy>(42));
        auto list = storedQueue(store);
        QVERIFY(player.replaceQueue(list));
        QVERIFY(player.setPlaybackMode(PlaybackMode::Shuffle));
        PlaybackOrderService expected(std::make_unique<ShuffleBagStrategy>(42));
        expected.reset(list);
        QVERIFY(expected.setMode(PlaybackMode::Shuffle, list));
        const int proposed = expected.propose(list, PlaybackAdvance::Next).queueId;
        audio.seek(15000);
        const int loads = audio.loads;
        QSqlQuery sql(store.db.database());
        QVERIFY(sql.exec("CREATE TRIGGER fail_order BEFORE DELETE ON queue_items BEGIN SELECT RAISE(FAIL, 'injected'); END"));
        for (int i = 0; i < 3; ++i) QVERIFY(!player.next());
        QCOMPARE(selected(player), 1);
        QCOMPARE(audio.clock, 15000);
        QCOMPARE(audio.loads, loads);
        QVERIFY(audio.playing);
        QSignalSpy errors(&player, &PlayerEngine::errorOccurred);
        emit audio.ended();
        QCOMPARE(errors.count(), 1);
        QCOMPARE(selected(player), 1);
        QVERIFY(sql.exec("DROP TRIGGER fail_order"));
        QVERIFY(player.next());
        QCOMPARE(selected(player), proposed);
        QVERIFY(player.pause());
        const int current = selected(player);
        QVERIFY(player.play());
        QCOMPARE(selected(player), current);
        QVERIFY(player.previous());
        QCOMPARE(selected(player), 1);
        QVERIFY(player.next());
        QCOMPARE(selected(player), current);
        QVERIFY(player.playItem(3));
        QVERIFY(player.previous());
        QCOMPARE(selected(player), 3);
        QVERIFY(player.clear());
        emit audio.ended();
        QCOMPARE(errors.count(), 1); // Empty natural end is harmless.
        QVERIFY(!player.next());
        QVERIFY(!player.previous());
    }
    // Delete an occurrence that lies inside the recorded shuffle history.
    // Previous must skip the removed identity rather than addressing an old queue index.
    // Deleting the current entry selects a survivor and branches history consistently.
    // Replacing the queue starts a new navigation context with fresh occurrence identity.
    // A one-item replacement cannot draw a nonexistent alternative.
    // Switching modes resets order policy without reviving removed history entries.
    void historySurvivesDeletionAndQueueReplacement() {
        QTemporaryDir dir;
        StoreFixture store(dir.filePath("songs.sqlite3"));
        QueueService queues(store.queueRepo, store.songRepo, store.db);
        OrderAudio audio;
        PlayerEngine player(audio, queues, std::make_unique<ShuffleBagStrategy>(17));
        auto list = storedQueue(store);
        QVERIFY(player.replaceQueue(list));
        QVERIFY(player.setPlaybackMode(PlaybackMode::Shuffle));
        QList<int> history{1};
        for (int i = 0; i < 3; ++i) {
            QVERIFY(player.next());
            history.append(selected(player));
        }
        QVERIFY(player.removeItem(history.at(1))); // Remove a historical item, skipping it on Previous.
        QVERIFY(player.previous());
        QCOMPARE(selected(player), history.at(2));
        QVERIFY(player.previous());
        QCOMPARE(selected(player), 1);
        QVERIFY(player.removeItem(1)); // Removing the current item selects a surviving queue entry.
        const int replacement = selected(player);
        QVERIFY(player.previous());
        QCOMPARE(selected(player), replacement);
        QVERIFY(player.next());
        QVERIFY(selected(player) != replacement);
        auto next = queues.queue();
        next.clear();
        const auto metadata = store.songById(1);
        QVERIFY(metadata);
        const int id = next.add("/music/new-context.wav", *metadata);
        next.setCurrentIndex(0);
        QVERIFY(player.replaceQueue(next));
        QVERIFY(player.previous());
        QCOMPARE(selected(player), id);
        emit audio.ended();
        QCOMPARE(selected(player), id);
        QVERIFY(player.setPlaybackMode(PlaybackMode::Sequential));
        QVERIFY(player.setPlaybackMode(PlaybackMode::Shuffle));
        QVERIFY(player.previous());
        QCOMPARE(selected(player), id);
    }
    // Failed preference persistence must leave both the mode and pending shuffle draw intact.
    // Single-item behavior differs for natural repeat versus explicit navigation.
    // Every mode must produce the stop sentinel on an empty queue.
    // The mode saver is injected so failure is deterministic without permissions tricks.
    // These boundaries protect changes that otherwise only affect normal multi-item lists.
    void modeSaveFailureAndEmptyQueue() {
        auto list = queue();
        PlaybackOrderService order(std::make_unique<ShuffleBagStrategy>(42), PlaybackMode::Shuffle,
                                   [](PlaybackMode) { return false; });
        order.reset(list);
        const int proposed = order.propose(list, PlaybackAdvance::Next).queueId;
        QVERIFY(!order.setMode(PlaybackMode::RepeatAll, list));
        QCOMPARE(int(order.mode()), int(PlaybackMode::Shuffle));
        QCOMPARE(order.propose(list, PlaybackAdvance::Next).queueId, proposed);
        for (auto mode : {PlaybackMode::Sequential, PlaybackMode::RepeatOne, PlaybackMode::Shuffle, PlaybackMode::RepeatAll}) {
            PlaybackOrderService empty(std::make_unique<ShuffleBagStrategy>(42), mode);
            auto one = queue(1); empty.reset(one);
            QCOMPARE(empty.propose(one, PlaybackAdvance::Ended).queueId, mode == PlaybackMode::Sequential ? 0 : 1);
            QCOMPARE(empty.propose(one, PlaybackAdvance::Next).queueId,
                     mode == PlaybackMode::Sequential || mode == PlaybackMode::RepeatOne ? 0 : 1);
            QCOMPARE(empty.propose(one, PlaybackAdvance::Previous).queueId, 1);
            auto none = queue(0); empty.reset(none);
            QCOMPARE(empty.propose(none, PlaybackAdvance::Ended).queueId, 0);
        }
    }
    // An unknown saved mode falls back safely at backend startup.
    // An accepted mode change reaches the initiating response and another client's event stream.
    // The frontend controller must reconcile the same confirmed mode.
    // Reopening verifies the setting survives beyond the active session.
    // Invalid wire mode strings must be rejected rather than normalized silently.
    // This links domain policy, persistence, socket events and frontend state.
    void ipcPersistenceAndClientSync() {
        QVERIFY(AppPaths::saveSetting("playback_mode", "unknown_mode"));
        BackendRuntime runtime;
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        OrderPeer first, second;
        QVERIFY(first.connect(runtime.serverName()));
        QVERIFY(second.connect(runtime.serverName()));
        QCOMPARE(first.event("server.connected").value("data").toObject().value("playback_mode").toString(), "sequential");
        IpcClient client;
        PlaybackController controller(client);
        client.connectBackend();
        QTRY_VERIFY(client.connected());
        for (const auto &mode : {"repeat_one", "shuffle", "repeat_all", "sequential"}) {
            const auto response = first.call("player.set_playback_mode", {{"mode", mode}});
            QCOMPARE(response.value("status").toString(), "ok");
            QCOMPARE(response.value("data").toObject().value("playback_mode").toString(), mode);
            QCOMPARE(second.event("player.playback_mode_changed").value("playback_mode").toString(), mode);
            QTRY_COMPARE(controller.playbackMode(), QString(mode));
        }
        for (const auto &bad : {QJsonValue(), QJsonValue(1), QJsonValue("bad"), QJsonValue(true)})
            QCOMPARE(first.call("player.set_playback_mode", {{"mode", bad}}).value("status").toString(), "error");
        controller.setPlaybackMode("shuffle");
        QTRY_COMPARE(controller.playbackMode(), QString("shuffle"));
        QCOMPARE(AppPaths::setting("playback_mode").toString(), "shuffle");
        QVERIFY(AppPaths::setting("lyrics_offline").toBool());
        runtime.stop();
        QVERIFY(runtime.start());
        OrderPeer restored;
        QVERIFY(restored.connect(runtime.serverName()));
        QCOMPARE(restored.event("server.connected").value("data").toObject().value("playback_mode").toString(), "shuffle");
        QCOMPARE(restored.call("player.status").value("data").toObject().value("playback_mode").toString(), "shuffle");
        // Corrupt settings exercise the existing atomic writer's refusal to overwrite other settings.
        QFile settings(AppPaths::configFile("settings.json"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("broken"); settings.close();
        QCOMPARE(restored.call("player.set_playback_mode", {{"mode", "repeat_all"}}).value("status").toString(), "error");
        QCOMPARE(restored.call("player.status").value("data").toObject().value("playback_mode").toString(), "shuffle");
        runtime.stop();
    }
};
QTEST_GUILESS_MAIN(PlaybackOrderTest)
#include "playback_order_test.moc"
