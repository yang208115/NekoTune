#include "application/playback/audio_output_service.h"
#include "application/playback/player_engine.h"
#include "application/library/collection_service.h"
#include "app_paths.h"
#include "controllers/audio_output_controller.h"
#include "controllers/app_controllers.h"
#include "i18n.h"
#include "runtime/backend_runtime.h"
#include "support/audio_backend.h"
#include "support/store_fixture.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

using namespace nekotune;
namespace {
const QString speaker = "c3BlYWtlcg==";
const QString headset = "aGVhZHNldA==";
QList<AudioOutputDevice> twoDevices(bool headsetDefault = false) {
    return {{speaker, "Same name", !headsetDefault}, {headset, "Same name", headsetDefault}};
}
class OutputAudio final : public TestAudioBackend {
  public:
    QUrl url;
    qint64 clock = 0;
    double level = .8;
    int loads = 0, starts = 0;
    int portWrites = 0;
    bool failPort = false, delayPort = false;
    OutputCompletion portCompletion;
    void setAudioOutputPort(const QString &deviceId, const QString &portId, OutputCompletion done) override {
        ++portWrites;
        auto apply = [this, deviceId, portId, done = std::move(done)](Result<void> result) mutable {
            if (!result) { done(std::move(result)); return; }
            for (auto &device : devices)
                if (device.id == deviceId) device.activePortId = portId;
            emit audioOutputsChanged();
            done({});
        };
        if (delayPort) portCompletion = std::move(apply);
        else apply(failPort ? Result<void>{failure("audio_output_port_switch_failed")} : Result<void>{});
    }
    void setSource(const QUrl &value) override { url = value; clock = 0; ++loads; }
    QUrl source() const override { return url; }
    void play() override { ++starts; emit stateChanged(PlayerState::Playing); }
    void pause() override { emit stateChanged(PlayerState::Paused); }
    void stop() override { clock = 0; emit stateChanged(PlayerState::Stopped); }
    void seek(qint64 value) override { clock = value; }
    void setVolume(double value) override { level = value; }
    qint64 position() const override { return clock; }
    qint64 duration() const override { return 60000; }
    double volume() const override { return level; }
    AudioMetadata metadata() const override { return {}; }
};
struct Fixture {
    QTemporaryDir directory;
    StoreFixture store{directory.filePath("songs.db")};
    QueueService queue{store.queueRepo, store.songRepo, store.db};
    OutputAudio audio;
    PlayerEngine player{audio, queue};
    bool load() {
        auto song = store.getOrCreateSong("audio-output-song", "/music/test.wav");
        if (!song) return false;
        PlayerQueue list;
        list.add("/music/test.wav", *song);
        list.add("/music/test.wav", *song);
        list.setCurrentIndex(0);
        return bool(player.replaceQueue(list));
    }
};
QList<AudioOutputDevice> analogPorts(bool connected = true, bool headphonesActive = true) {
    return {{speaker, "Internal audio", true,
             {{"speaker", "Speakers", "speaker", true}, {"headphones", "Headphones", "headphones", connected}},
             headphonesActive ? "headphones" : "speaker"}};
}
class DelayedOutputSource final : public ISourceResolver {
  public:
    Completion pending;
    void resolve(const SongMetadata &, Completion done) override { pending = std::move(done); }
    void release(const QUrl &) override {}
};
class Peer {
  public:
    QLocalSocket socket;
    QByteArray buffer;
    QList<QJsonObject> events;
    int id = 0;
    QJsonObject call(const QString &method, QJsonObject params = {}) {
        ++id;
        socket.write(QJsonDocument(QJsonObject{{"id", id}, {"method", method}, {"params", params}})
                         .toJson(QJsonDocument::Compact) + '\n');
        socket.flush();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            buffer += socket.readAll();
            while (buffer.contains('\n')) {
                const auto end = buffer.indexOf('\n');
                const auto message = QJsonDocument::fromJson(buffer.left(end)).object();
                buffer.remove(0, end + 1);
                if (message.value("id").toInt(-1) == id)
                    return message;
                events.append(message);
            }
            socket.waitForReadyRead(20);
        }
        return {};
    }
};
} // namespace

class AudioOutputTest final : public QObject {
    Q_OBJECT
    QTemporaryDir m_home;
  private slots:
    void initTestCase() {
        qputenv("NEKOTUNE_HOME", m_home.path().toUtf8());
        qputenv("NEKOTUNE_DB_PATH", m_home.filePath("runtime.db").toUtf8());
        qputenv("NEKOTUNE_SOCKET", m_home.filePath("runtime.sock").toUtf8());
        QVERIFY(AppPaths::prepare());
        QVERIFY(AppPaths::saveSetting("lyrics_offline", true));
    }
    void selectionPersistsByIdAndSwitchPreservesPlayback_data() {
        QTest::addColumn<bool>("paused");
        QTest::addColumn<double>("volume");
        QTest::newRow("playing") << false << .35;
        QTest::newRow("paused-muted") << true << 0.;
    }
    void selectionPersistsByIdAndSwitchPreservesPlayback() {
        QFETCH(bool, paused);
        QFETCH(double, volume);
        Fixture f;
        f.audio.devices = twoDevices();
        AudioOutputSelection saved;
        AudioOutputService output(f.audio, {}, [&](const auto &value) { saved = value; return true; });
        QCOMPARE(output.snapshot().activeId, speaker);
        QVERIFY(f.load());
        if (paused) QVERIFY(f.player.pause());
        QVERIFY(f.player.seek(15000));
        QVERIFY(f.player.setVolume(volume));
        const auto before = f.player.snapshot();
        const auto loads = f.audio.loads, starts = f.audio.starts;
        QVERIFY(output.select(headset));
        QCOMPARE(saved.id, headset);
        QCOMPARE(output.snapshot().activeId, headset);
        QCOMPARE(f.audio.loads, loads);
        QCOMPARE(f.audio.starts, starts);
        QCOMPARE(int(f.player.snapshot().state), int(before.state));
        QCOMPARE(f.player.snapshot().position, before.position);
        QCOMPARE(f.player.snapshot().volume, volume);
        QCOMPARE(f.player.snapshot().song->id, before.song->id);
        QCOMPARE(f.queue.queue().size(), 2);
        OutputAudio restarted;
        restarted.devices = twoDevices();
        AudioOutputService restored(restarted, saved);
        QCOMPARE(restored.snapshot().activeId, headset);
        QCOMPARE(restarted.starts, 0);
    }
    void invalidAndFailedSavesRetainSelection() {
        Fixture f;
        f.audio.devices = twoDevices();
        int saves = 0;
        AudioOutputService output(f.audio, {}, [&](const auto &) { ++saves; return false; });
        QVERIFY(!output.select("unknown"));
        QCOMPARE(saves, 0);
        auto failed = output.select(headset);
        QVERIFY(!failed);
        QCOMPARE(failed.error().code, ErrorCode::Storage);
        QCOMPARE(saves, 1);
        QVERIFY(output.snapshot().selected.id.isEmpty());
        QCOMPARE(output.snapshot().activeId, speaker);
    }
    void disconnectPausesWithoutFallbackOrQueueDeletion_data() {
        QTest::addColumn<bool>("followDefault");
        QTest::newRow("explicit") << false;
        QTest::newRow("default") << true;
    }
    void disconnectPausesWithoutFallbackOrQueueDeletion() {
        QFETCH(bool, followDefault);
        Fixture f;
        f.audio.devices = twoDevices(true);
        AudioOutputService output(f.audio);
        if (!followDefault) QVERIFY(output.select(headset));
        QVERIFY(f.load());
        QVERIFY(f.player.seek(12000));
        const auto loads = f.audio.loads, starts = f.audio.starts;
        f.audio.setDevices({{speaker, "Speaker", true}});
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Paused));
        QCOMPARE(f.audio.clock, 12000);
        QCOMPARE(f.audio.loads, loads);
        QCOMPARE(f.queue.queue().size(), 2);
        QCOMPARE(output.snapshot().activeId, followDefault ? speaker : QString());
        QCOMPARE(output.snapshot().selected.id, followDefault ? QString() : headset);
        emit f.audio.audioOutputError("Disconnected");
        QCOMPARE(f.queue.queue().size(), 2);
        if (!followDefault) QVERIFY(!f.player.play());
        f.audio.setDevices(twoDevices(true));
        QCOMPARE(output.snapshot().activeId, headset);
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Paused));
        QCOMPARE(f.audio.starts, starts);
        QVERIFY(f.player.play());
        QCOMPARE(f.audio.starts, starts + 1);
        // A genuine media error still follows the existing decoder failure policy.
        emit f.audio.failed("Invalid audio format");
        QCOMPARE(f.queue.queue().size(), 1);
    }
    void defaultChangeKeepsPlayingAndExplicitChoiceIgnoresIt() {
        Fixture f;
        f.audio.devices = twoDevices();
        AudioOutputService output(f.audio);
        QVERIFY(f.load());
        QVERIFY(f.player.seek(7000));
        const auto starts = f.audio.starts;
        f.audio.setDevices(twoDevices(true));
        QCOMPARE(output.snapshot().activeId, headset);
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Playing));
        QCOMPARE(f.audio.clock, 7000);
        QCOMPARE(f.audio.starts, starts);
        QVERIFY(output.select(speaker));
        f.audio.setDevices(twoDevices(true));
        QCOMPARE(output.snapshot().activeId, speaker);
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Playing));
    }
    void noDevicesAndSavedOfflineDeviceNeverFallBack() {
        Fixture f;
        f.audio.devices.clear();
        AudioOutputService output(f.audio, {headset, "Saved headset"});
        QVERIFY(!output.snapshot().available);
        QCOMPARE(output.snapshot().selected.name, QString("Saved headset"));
        QVERIFY(f.load());
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Paused));
        QVERIFY(!f.player.play());
        QVERIFY(f.player.next());
        QCOMPARE(f.audio.starts, 0);
        f.audio.setDevices({{speaker, "Speaker", true}});
        QVERIFY(!output.snapshot().available);
        QVERIFY(output.select(""));
        QVERIFY(output.snapshot().available);
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Paused));
        QCOMPARE(f.audio.starts, 0);
        QVERIFY(f.player.play());
        f.audio.setDevices({});
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Paused));
        f.audio.setDevices(twoDevices());
        QCOMPARE(f.audio.starts, 1);
    }
    void lateOnlineResolutionDoesNotResumeAfterDisconnect() {
        Fixture f;
        f.audio.devices = twoDevices(true);
        DelayedOutputSource source;
        f.player.setSourceResolver(&source);
        AudioOutputService output(f.audio, {headset, "Headset"});
        CollectionService collections(f.store.songRepo, f.store.queueRepo, f.store.playlistRepo, f.store.db,
                                      f.store.library, f.queue, f.player);
        SongMetadata song;
        song.providerId = "test/source";
        song.providerTrackId = "track";
        QVERIFY(collections.enqueueRemote(song, true));
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Loading));
        QVERIFY(bool(source.pending));
        f.audio.setDevices({{speaker, "Speaker", true}});
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Paused));
        f.audio.setDevices(twoDevices(true));
        source.pending(QUrl("https://example.invalid/audio.wav"));
        QCOMPARE(f.audio.starts, 0);
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Paused));
        QVERIFY(f.player.play());
        QCOMPARE(f.audio.starts, 1);
        f.player.shutdown();
    }
    void portsOfOneCardAreDistinctAndConfirmedAfterSwitch() {
        Fixture f;
        f.audio.devices = analogPorts();
        AudioOutputSelection saved;
        AudioOutputService output(f.audio, {}, [&](const auto &selection) { saved = selection; return true; });
        QVERIFY(f.load());
        QVERIFY(f.player.seek(16000));
        f.audio.delayPort = true;
        bool completed = false;
        output.selectPort(speaker, "speaker", [&](Result<void> result) { QVERIFY(result); completed = true; });
        QVERIFY(!completed);
        QVERIFY(output.snapshot().selected.portId.isEmpty());
        QVERIFY(!output.select(""));
        auto completion = std::move(f.audio.portCompletion);
        completion({});
        QVERIFY(completed);
        QCOMPARE(saved.portId, QString("speaker"));
        QCOMPARE(output.snapshot().selected.portId, QString("speaker"));
        QCOMPARE(f.audio.devices.first().activePortId, QString("speaker"));
        QCOMPARE(f.audio.clock, 16000);
        QCOMPARE(f.audio.starts, 1);
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Playing));
    }
    void portFailureAndSaveRollbackPreservePreference() {
        Fixture f;
        f.audio.devices = analogPorts();
        AudioOutputService output(f.audio, {speaker, "Internal audio", "headphones", "Headphones"}, [](const auto &) { return false; });
        QVERIFY(f.load());
        f.audio.failPort = true;
        bool failed = false;
        output.selectPort(speaker, "speaker", [&](Result<void> result) { failed = !result; });
        QVERIFY(failed);
        QCOMPARE(output.snapshot().selected.portId, QString("headphones"));
        QCOMPARE(f.audio.devices.first().activePortId, QString("headphones"));
        f.audio.failPort = false;
        output.selectPort(speaker, "speaker", [&](Result<void> result) {
            QVERIFY(!result);
            QCOMPARE(result.error().code, ErrorCode::Storage);
        });
        QCOMPARE(f.audio.portWrites, 3); // Failed write, successful write, then restoration.
        QCOMPARE(f.audio.devices.first().activePortId, QString("headphones"));
        QCOMPARE(output.snapshot().selected.portId, QString("headphones"));
        QVERIFY(output.snapshot().available);
        f.audio.delayPort = true;
        bool lastAvailable = true, restoreFailed = false;
        connect(&output, &AudioOutputService::changed, this, [&] { lastAvailable = output.snapshot().available; });
        output.selectPort(speaker, "speaker", [&](Result<void> result) {
            restoreFailed = !result && result.error().message == "audio_output_restore_failed";
        });
        auto applied = std::move(f.audio.portCompletion);
        applied({});
        auto rollback = std::move(f.audio.portCompletion);
        rollback(failure("Native restore failed"));
        QVERIFY(restoreFailed);
        QVERIFY(!output.snapshot().available);
        QVERIFY(!lastAvailable); // The broadcast must describe the suspended output too.
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Paused));
    }
    void unpluggingHeadphonesPausesEvenWhenTheCardRemains_data() {
        QTest::addColumn<bool>("pinned");
        QTest::newRow("default") << false;
        QTest::newRow("headphones") << true;
    }
    void unpluggingHeadphonesPausesEvenWhenTheCardRemains() {
        QFETCH(bool, pinned);
        Fixture f;
        f.audio.devices = analogPorts();
        AudioOutputService output(f.audio, pinned ? AudioOutputSelection{speaker, "Internal audio", "headphones", "Headphones"} : AudioOutputSelection{});
        QVERIFY(f.load());
        QVERIFY(f.player.seek(17000));
        f.audio.setDevices(analogPorts(false, false));
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Paused));
        QCOMPARE(f.audio.clock, 17000);
        QCOMPARE(output.snapshot().available, !pinned);
        QCOMPARE(f.queue.queue().size(), 2);
        f.audio.setDevices(analogPorts());
        QVERIFY(output.snapshot().available);
        QCOMPARE(f.audio.starts, 1);
        QCOMPARE(int(f.player.snapshot().state), int(PlayerState::Paused));
    }
    void offlinePortRestoresWithoutAutoPlayback() {
        Fixture f;
        f.audio.devices = analogPorts(false, false);
        AudioOutputService output(f.audio, {speaker, "Internal audio", "headphones", "Headphones"});
        QVERIFY(!output.snapshot().available);
        QVERIFY(f.load());
        f.audio.setDevices(analogPorts(true, false));
        QVERIFY(output.snapshot().available);
        QCOMPARE(f.audio.devices.first().activePortId, QString("headphones"));
        QCOMPARE(f.audio.starts, 0);
        bool rejected = false;
        output.selectPort(speaker, "absent", [&](Result<void> result) { rejected = !result; });
        QVERIFY(rejected);
        f.audio.setDevices({});
        QVERIFY(!output.snapshot().available);
        f.audio.setDevices(analogPorts(true, false));
        QVERIFY(output.snapshot().available);
        QCOMPARE(f.audio.devices.first().activePortId, QString("headphones"));
        QCOMPARE(f.audio.starts, 0);
    }
    void ipcControllerPersistenceAndReconnect() {
        QVERIFY(AppPaths::saveSetting("audio_output", QJsonObject{{"device_id", "saved-offline-id"},
                                                                  {"device_name", "Offline headset"}}));
        BackendRuntime runtime;
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        Peer peer;
        peer.socket.connectToServer(runtime.serverName());
        QVERIFY(peer.socket.waitForConnected(3000));
        const auto snapshot = peer.call("player.audio_outputs").value("data").toObject();
        QVERIFY(snapshot.value("devices").isArray());
        QCOMPARE(snapshot.value("audio_output").toObject().value("selected_id").toString(), "saved-offline-id");
        QVERIFY(!snapshot.value("audio_output").toObject().value("available").toBool());
        QCOMPARE(peer.events.first().value("event").toString(), "server.connected");
        QCOMPARE(peer.events.first().value("data").toObject().value("audio_output"), snapshot.value("audio_output"));
        QCOMPARE(peer.call("player.status").value("data").toObject().value("audio_output"), snapshot.value("audio_output"));
        for (const auto &params : {QJsonObject{}, QJsonObject{{"device_id", 12}}, QJsonObject{{"device_id", "absent"}}})
            QCOMPARE(peer.call("player.set_audio_output", params).value("status").toString(), "error");
        IpcClient client;
        AudioOutputController controller(client);
        client.connectBackend();
        QTRY_COMPARE(controller.output().value("selected_id").toString(), QString("saved-offline-id"));
        QSignalSpy succeeded(&controller, &FeatureController::requestSucceeded);
        controller.select("");
        QVERIFY(controller.busy());
        controller.select("absent"); // A pending selection cannot be overwritten by a second click.
        QTRY_VERIFY(!controller.busy());
        QVERIFY(controller.error().isEmpty());
        QVERIFY(controller.output().value("selected_id").toString().isEmpty());
        QCOMPARE(AppPaths::setting("audio_output").toObject().value("device_id").toString(), QString());
        QVERIFY(AppPaths::setting("lyrics_offline").toBool());
        controller.select("absent");
        QTRY_VERIFY(!controller.busy());
        QCOMPARE(controller.error(), QString("audio_output_device_unavailable"));
        QVERIFY(client.error().isEmpty());
        const auto updated = peer.call("player.audio_outputs").value("data").toObject();
        bool gotEvent = false;
        for (const auto &event : peer.events)
            if (event.value("event") == "player.audio_outputs_changed") {
                QCOMPARE(event.value("audio_output"), updated.value("audio_output"));
                QCOMPARE(event.value("devices"), updated.value("devices"));
                gotEvent = true;
            }
        QVERIFY(gotEvent);
        runtime.stop();
        QTRY_VERIFY(!client.connected());
        QVERIFY(runtime.start());
        client.connectBackend();
        QTRY_VERIFY(client.connected());
        QTRY_VERIFY(controller.error().isEmpty());
        QVERIFY(controller.output().value("selected_id").toString().isEmpty());
        QTRY_COMPARE(controller.devices(), updated.value("devices").toArray().toVariantList());
        runtime.stop();
    }
    void realSettingsAndPlayerShareDeviceState() {
        QVERIFY(AppPaths::saveSetting("audio_output", QJsonObject{}));
        BackendRuntime runtime;
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        IpcClient client;
        AppControllers controllers(client);
        QSignalSpy responses(&client, &IpcClient::responseReceived);
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
        window->setProperty("viewMode", "settings");
        client.connectBackend();
        QTRY_VERIFY(controllers.audioOutput->output().contains("available"));
        auto receivedList = [&] {
            for (const auto &response : responses)
                if (response.first().toString() == "player.audio_outputs")
                    return true;
            return false;
        };
        QTRY_VERIFY(receivedList());
        auto settingsPage = [&]() -> QObject * {
            QVariant result;
            QMetaObject::invokeMethod(window, "pageItem", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, QString("settings")));
            return result.value<QObject *>();
        };
        QTRY_VERIFY(settingsPage());
        auto *picker = settingsPage()->findChild<QQuickItem *>("settingsAudioOutputPicker");
        QVERIFY(picker);
        QCOMPARE(picker->property("controller").value<QObject *>(), controllers.audioOutput);
        auto *button = window->findChild<QQuickItem *>("audioOutputButton");
        QVERIFY(button);
        QCOMPARE(button->property("controller").value<QObject *>(), controllers.audioOutput);
        // This integration uses real enumeration, but never starts audio on the host.
        if (!controllers.audioOutput->devices().isEmpty()) {
            const auto id = controllers.audioOutput->devices().first().toMap().value("id").toString();
            controllers.audioOutput->select(id);
            QTRY_VERIFY(!controllers.audioOutput->busy());
            QVERIFY(controllers.audioOutput->error().isEmpty());
            QCOMPARE(controllers.audioOutput->output().value("active_id").toString(), id);
            QTRY_COMPARE(picker->property("selectedId").toString(), id);
        }
        auto *popup = button->findChild<QObject *>("audioOutputPopup");
        QVERIFY(popup);
        for (const auto &language : {QString("zh"), QString("en")}) {
            translator.setLanguage(language);
            window->resize(language == "zh" ? 1000 : 1440, 800);
            QTest::qWait(50);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
            QTRY_VERIFY(popup->property("opened").toBool());
            if (const auto directory = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT_DIR"); !directory.isEmpty()) {
                QTest::qWait(150);
                QVERIFY(window->grabWindow().save(directory + "/audio-output-real-" + language + ".png"));
            }
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!popup->property("visible").toBool());
        }
        runtime.stop();
    }
};
QTEST_MAIN(AudioOutputTest)
#include "audio_output_test.moc"
