#include "infrastructure/ai/ai_backend.h"
#include "infrastructure/ai/ai_settings.h"
#include "infrastructure/lyrics/lyrics_cache.h"
#include "controllers/ai_controller.h"
#include "runtime/backend_runtime.h"
#include "app_paths.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
using namespace nekotune;
namespace {
class MemoryCredentials final : public CredentialStore {
  public:
    QHash<QString, QByteArray> values;
    bool locked = false;
    bool loseWrite = false;
    int readDelayMs = 0;
    Result<std::optional<QByteArray>> read(const QString &id) override {
        if (readDelayMs)
            QThread::msleep(readDelayMs);
        if (locked)
            return failure("locked");
        return values.contains(id) ? std::optional<QByteArray>{values.value(id)}
                                   : std::optional<QByteArray>{};
    }
    Result<void> write(const QString &id, const QByteArray &secret) override {
        if (locked)
            return failure("locked");
        if (!loseWrite)
            values[id] = secret;
        return {};
    }
    Result<void> remove(const QString &id) override {
        if (locked)
            return failure("locked");
        values.remove(id);
        return {};
    }
};
QByteArray completion(const QJsonObject &result) {
    return QJsonDocument(
               QJsonObject{
                   {"choices",
                    QJsonArray{QJsonObject{
                        {"finish_reason", "stop"},
                        {"message", QJsonObject{{"content", QString::fromUtf8(QJsonDocument(result).toJson(
                                                                QJsonDocument::Compact))}}}}}}})
        .toJson(QJsonDocument::Compact);
}
const QJsonObject validSuggestion{
    {"custom_title", "夜空"}, {"artist", "测试歌手"}, {"tags", QJsonArray{" 中文 ", "中文", "POP"}}};
class HttpFixture final : public QTcpServer {
  public:
    struct Response {
        int status = 200;
        QByteArray body = completion(validSuggestion);
        int delay = 0;
    };
    QList<Response> responses;
    QList<QJsonObject> requests;
    QList<QByteArray> headers;
    HttpFixture() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    *buffer += socket->readAll();
                    const auto end = buffer->indexOf("\r\n\r\n");
                    if (end < 0 || socket->property("handled").toBool())
                        return;
                    qsizetype length = 0;
                    for (const auto &line : buffer->left(end).split('\n'))
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(15).trimmed().toInt();
                    if (buffer->size() < end + 4 + length)
                        return;
                    socket->setProperty("handled", true);
                    headers.append(buffer->left(end));
                    requests.append(QJsonDocument::fromJson(buffer->mid(end + 4, length)).object());
                    const auto response = responses.isEmpty() ? Response{} : responses.takeFirst();
                    if (response.delay < 0)
                        return;
                    QTimer::singleShot(response.delay, socket, [socket, response] {
                        socket->write("HTTP/1.1 " + QByteArray::number(response.status) +
                                      " Result\r\nContent-Type: application/json\r\nConnection: "
                                      "close\r\nContent-Length: " +
                                      QByteArray::number(response.body.size()) + "\r\n\r\n" + response.body);
                        socket->disconnectFromHost();
                    });
                });
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QString baseUrl() const { return "http://127.0.0.1:" + QString::number(serverPort()) + "/gateway/v1"; }
};
template <class T, class Start> Result<T> waitFor(Start start) {
    auto result = std::make_shared<std::optional<Result<T>>>();
    start([result](Result<T> value) { *result = std::move(value); });
    QElapsedTimer clock;
    clock.start();
    while (!*result && clock.elapsed() < 5000)
        QTest::qWait(5);
    return *result ? **result : Result<T>{failure("Test callback timed out")};
}
Result<AiSuggestion> generate(AiBackend &backend, const AiMetadataInput &input = {}) {
    return waitFor<AiSuggestion>([&](auto done) { backend.suggest(input, done); });
}
class Peer {
  public:
    QLocalSocket socket;
    QByteArray buffer;
    int nextId = 0;
    int send(const QString &method, const QJsonObject &params = {}) {
        const int id = ++nextId;
        socket.write(QJsonDocument(QJsonObject{{"id", id}, {"method", method}, {"params", params}})
                         .toJson(QJsonDocument::Compact) +
                     '\n');
        socket.flush();
        return id;
    }
    QJsonObject receive(int id) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            buffer += socket.readAll();
            while (buffer.contains('\n')) {
                auto index = buffer.indexOf('\n');
                const auto data = QJsonDocument::fromJson(buffer.left(index)).object();
                buffer.remove(0, index + 1);
                if (data.value("id").toInt() == id)
                    return data;
            }
            QTest::qWait(5);
        }
        return {};
    }
    QJsonObject call(const QString &method, const QJsonObject &params = {}) {
        return receive(send(method, params));
    }
};
} // namespace
class AiTest final : public QObject {
    Q_OBJECT
    QTemporaryDir m_directory;
    int m_profile = 0;
  private slots:
    void init() {
        qputenv("NEKOTUNE_HOME", m_directory.filePath(QString::number(++m_profile)).toUtf8());
        qputenv("NEKOTUNE_DB_PATH", AppPaths::databasePath().toUtf8());
        qputenv("NEKOTUNE_SOCKET", m_directory.filePath(QString::number(m_profile) + ".sock").toUtf8());
        QVERIFY(AppPaths::prepare());
    }
    void configIsPersistentAndKeysAreIsolated() {
        auto store = std::make_shared<MemoryCredentials>();
        AiSettings settings(store);
        QVERIFY(!settings.configuration().configured());
        QVERIFY(settings.configure({"https://example.com/proxy/v1/", "model-a", "test-secret"}));
        auto config = settings.configuration();
        QCOMPARE(config.baseUrl, QString("https://example.com/proxy/v1"));
        QVERIFY(config.keySaved);
        QCOMPARE(settings.key(config).value(), QByteArray("test-secret"));
        QFile file(AppPaths::configFile("settings.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("test-secret"));
        QVERIFY(settings.configure({"https://other.example/v1", "model-b"}));
        QVERIFY(!settings.configuration().keySaved);
        QVERIFY(settings.key(settings.configuration()).value().isEmpty());
        QVERIFY(settings.configure({"https://example.com/proxy/v1", "model-c"}));
        AiSettings reloaded(store);
        QCOMPARE(reloaded.configuration().model, QString("model-c"));
        QCOMPARE(reloaded.key(reloaded.configuration()).value(), QByteArray("test-secret"));
        const auto configBytes = AppPaths::setting("ai");
        const auto oldHome = qgetenv("NEKOTUNE_HOME");
        qputenv("NEKOTUNE_HOME", m_directory.filePath("other-profile").toUtf8());
        QVERIFY(AppPaths::saveSetting("ai", configBytes));
        QCOMPARE(reloaded.configuration().credentialError, QString("ai_error_key_missing"));
        qputenv("NEKOTUNE_HOME", oldHome);
        QVERIFY(reloaded.clearKey());
        QVERIFY(store->values.isEmpty());
        QVERIFY(!reloaded.configuration().keySaved);
    }
    void configValidationAndLockedKeyring() {
        auto store = std::make_shared<MemoryCredentials>();
        AiSettings settings(store);
        for (const auto &url :
             {"file:///tmp/key", "https://u:p@example.com/v1", "https://x.test/v1?key=secret", "invalid"})
            QVERIFY(!settings.configure({url, "model"}));
        QVERIFY(!settings.configure({"https://example.com/v1", ""}));
        QVERIFY(!settings.configure({"https://example.com/v1", "model", "bad\nkey"}));
        store->locked = true;
        QVERIFY(settings.configure({"https://example.com/v1", "model"}));
        QVERIFY(settings.configuration().credentialError.isEmpty());
        QVERIFY(!settings.configure({"https://example.com/v1", "model", "key"}));
        QVERIFY(!settings.configuration().keySaved);
        store->locked = false;
        QVERIFY(settings.configure({"https://example.com/v1", "model", "key"}));
        store->locked = true;
        QCOMPARE(settings.configuration().credentialError, QString("ai_error_keyring"));
        QVERIFY(!settings.clearKey());
    }
    void failedSettingsWriteRestoresPreviousKey() {
        auto store = std::make_shared<MemoryCredentials>();
        AiSettings settings(store);
        QVERIFY(settings.configure({"https://example.com/v1", "model", "old-key"}));
        const auto before = store->values;
        store->loseWrite = true;
        QVERIFY(!settings.configure({"https://example.com/v1", "model", "new-key"}));
        QCOMPARE(store->values, before);
        store->loseWrite = false;
        QFile file(AppPaths::configFile("settings.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{invalid");
        file.close();
        const auto saved = settings.configure({"https://example.com/v1", "model", "new-key"});
        QVERIFY(!saved);
        QCOMPARE(saved.error().message, QString("ai_error_settings"));
        QCOMPARE(store->values, before);
    }
    void usesDraftTextAndCanonicalTagsWithoutPaths() {
        HttpFixture server;
        auto store = std::make_shared<MemoryCredentials>();
        AiSettings settings(store);
        QVERIFY(settings.configure({server.baseUrl(), "test-model", "unit-test-secret"}));
        AiBackend backend(store);
        AiMetadataInput input;
        input.song = {1,
                      "test-hash",
                      "/private/music/managed/0001.mp3",
                      "old title",
                      "old artist",
                      "old lyrics",
                      "歌手 - 夜空"};
        input.draft.title = "draft title";
        input.draft.artist = "draft artist";
        input.draft.lyrics = "[00:01.00]我们仰望夜空\n[00:02.00]星光闪烁";
        input.currentTags = {"收藏"};
        input.availableTags = {"pop", "中文"};
        const auto result = generate(backend, input);
        QVERIFY2(bool(result), qPrintable(result.error().message));
        QCOMPARE(result.value().title, QString("夜空"));
        QCOMPARE(result.value().tags, QStringList({"中文", "pop"}));
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(server.headers.first().startsWith("POST /gateway/v1/chat/completions HTTP/1.1"));
        QVERIFY(server.headers.first().contains("Authorization: Bearer unit-test-secret"));
        const auto body = server.requests.first();
        QCOMPARE(body.value("model").toString(), QString("test-model"));
        QCOMPARE(body.value("response_format").toObject().value("type").toString(), QString("json_object"));
        const auto text = body.value("messages").toArray().last().toObject().value("content").toString();
        QVERIFY(!text.contains("/private/") && !text.contains("test-hash") &&
                !text.contains("unit-test-secret"));
        const auto evidence = QJsonDocument::fromJson(text.toUtf8()).object();
        QCOMPARE(evidence.value("filename").toString(), QString("歌手 - 夜空"));
        QCOMPARE(evidence.value("custom_title").toString(), QString("draft title"));
        QCOMPARE(evidence.value("lyrics").toString(), QString("我们仰望夜空\n星光闪烁"));
    }
    void localLyricsThenCacheAndInputLimits() {
        HttpFixture server;
        auto store = std::make_shared<MemoryCredentials>();
        AiSettings settings(store);
        QVERIFY(settings.configure({server.baseUrl(), "model"}));
        AiBackend backend(store);
        const auto path = m_directory.filePath("local-song.mp3");
        QFile lyrics(m_directory.filePath("local-song.lrc"));
        QVERIFY(lyrics.open(QIODevice::WriteOnly));
        lyrics.write("[00:01.00]Local sidecar words\n");
        lyrics.close();
        AiMetadataInput input;
        input.song.hash = "hash-local";
        input.song.firstPath = path;
        input.path = path;
        LyricsQuery query;
        query.trackId = input.song.hash;
        LyricsDocument cached;
        cached.source = "custom";
        cached.plainLyrics = "Cached words";
        QVERIFY(LyricsCache().write(query, cached));
        QVERIFY(generate(backend, input));
        auto evidence = [&] {
            return QJsonDocument::fromJson(server.requests.last()
                                               .value("messages")
                                               .toArray()
                                               .last()
                                               .toObject()
                                               .value("content")
                                               .toString()
                                               .toUtf8())
                .object();
        };
        QCOMPARE(evidence().value("lyrics").toString(), QString("Local sidecar words"));
        QVERIFY(lyrics.remove());
        QVERIFY(generate(backend, input));
        QCOMPARE(evidence().value("lyrics").toString(), QString("Cached words"));
        input.draft.lyrics = QString(50000, 'a');
        QVERIFY(generate(backend, input));
        QCOMPARE(evidence().value("lyrics").toString().size(), 12000);
    }
    void invalidResponses_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("error");
        QTest::newRow("not-json") << QByteArray("garbage") << 200 << QString("ai_error_response");
        QTest::newRow("wrong-types") << completion(
                                            {{"custom_title", 4}, {"artist", ""}, {"tags", QJsonArray{}}})
                                     << 200 << QString("ai_error_response");
        QTest::newRow("missing-tags")
            << completion({{"custom_title", "x"}, {"artist", ""}}) << 200 << QString("ai_error_response");
        QTest::newRow("bad-tag") << completion({{"custom_title", ""},
                                                {"artist", ""},
                                                {"tags", QJsonArray{QString(65, 'a')}}})
                                 << 200 << QString("ai_error_response");
        QTest::newRow("auth") << QByteArray("{\"error\":{\"message\":\"secret\"}}") << 401
                              << QString("ai_error_auth");
        QTest::newRow("rate-limited") << QByteArray("{}") << 429 << QString("ai_error_rate_limit");
        QTest::newRow("server") << QByteArray("{}") << 500 << QString("ai_error_service");
        QTest::newRow("redirect") << QByteArray("{}") << 302 << QString("ai_error_request");
        QTest::newRow("bad-model-no-retry") << QByteArray("{\"error\":{\"message\":\"model unsupported\"}}")
                                            << 400 << QString("ai_error_request");
        QTest::newRow("huge") << QByteArray(300000, 'x') << 200 << QString("ai_error_response");
    }
    void invalidResponses() {
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(QString, error);
        HttpFixture server;
        server.responses.append({status, body});
        auto store = std::make_shared<MemoryCredentials>();
        QVERIFY(AiSettings(store).configure({server.baseUrl(), "model"}));
        AiBackend backend(store);
        const auto result = generate(backend);
        QVERIFY(!result);
        QCOMPARE(result.error().message, error);
        QCOMPARE(server.requests.size(), 1);
    }
    void jsonModeFallbackIsBoundedAndUnknownNamesAreEmpty() {
        HttpFixture server;
        const QByteArray unsupported =
            R"({"error":{"param":"response_format","message":"json_object is not supported"}})";
        server.responses = {
            {400, unsupported},
            {200, completion({{"custom_title", ""}, {"artist", ""}, {"tags", QJsonArray{}}})}};
        auto store = std::make_shared<MemoryCredentials>();
        QVERIFY(AiSettings(store).configure({server.baseUrl(), "model"}));
        AiBackend backend(store);
        const auto result = generate(backend);
        QVERIFY(result);
        QVERIFY(result.value().title.isEmpty() && result.value().artist.isEmpty());
        QCOMPARE(result.value().warning, QString("ai_partial_result"));
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(server.requests.first().contains("response_format"));
        QVERIFY(!server.requests.last().contains("response_format"));
        server.responses = {{400, unsupported}, {400, unsupported}};
        QVERIFY(!generate(backend));
        QCOMPARE(server.requests.size(), 4);
    }
    void timeoutAndShutdownFinishExactlyOnce() {
        HttpFixture server;
        server.responses.append({200, {}, -1});
        auto store = std::make_shared<MemoryCredentials>();
        QVERIFY(AiSettings(store).configure({server.baseUrl(), "model"}));
        AiBackend shortTimeout(store, 80);
        const auto timedOut = generate(shortTimeout);
        QVERIFY(!timedOut);
        QCOMPARE(timedOut.error().message, QString("ai_error_timeout"));
        AiBackend backend(store);
        server.responses.append({200, {}, -1});
        int calls = 0;
        QString error;
        backend.suggest({}, [&](auto result) {
            ++calls;
            if (!result)
                error = result.error().message;
        });
        QTRY_COMPARE(server.requests.size(), 2);
        backend.shutdown();
        QCOMPARE(calls, 1);
        QCOMPARE(error, QString("ai_error_cancelled"));
        QTest::qWait(50);
        QCOMPARE(calls, 1);
    }
    void deadlineIncludesSlowKeyringAndCompatibilityRetry() {
        HttpFixture server;
        auto store = std::make_shared<MemoryCredentials>();
        QVERIFY(AiSettings(store).configure({server.baseUrl(), "model", "secret"}));
        store->readDelayMs = 150;
        {
            AiBackend backend(store, 60);
            QElapsedTimer elapsed;
            elapsed.start();
            const auto result = generate(backend);
            QVERIFY(!result);
            QCOMPARE(result.error().message, QString("ai_error_timeout"));
            QVERIFY(elapsed.elapsed() < 200);
        }
        QCOMPARE(server.requests.size(), 0);
        store->readDelayMs = 0;
        const QByteArray unsupported = R"({"error":{"param":"response_format","message":"unsupported"}})";
        server.responses = {{400, unsupported, 60}, {200, completion(validSuggestion), 100}};
        AiBackend backend(store, 120);
        const auto result = generate(backend);
        QVERIFY(!result);
        QCOMPARE(result.error().message, QString("ai_error_timeout"));
        QCOMPARE(server.requests.size(), 2);
    }
    void frontendDiscardsStaleRepliesAndRecoversAfterDisconnect() {
        QLocalServer server;
        QVERIFY(server.listen(qEnvironmentVariable("NEKOTUNE_SOCKET")));
        IpcClient client;
        AiController controller(client);
        QSignalSpy ready(&controller, &AiController::suggestionReady);
        QTRY_VERIFY(server.hasPendingConnections());
        auto *socket = server.nextPendingConnection();
        QTRY_VERIFY(client.connected());
        QByteArray received;
        QList<QJsonObject> requests;
        connect(socket, &QLocalSocket::readyRead, this, [&] {
            received += socket->readAll();
            while (received.contains('\n')) {
                const auto end = received.indexOf('\n');
                requests.append(QJsonDocument::fromJson(received.left(end)).object());
                received.remove(0, end + 1);
            }
        });
        auto respond = [&](int index, const QString &title) {
            const auto request = requests.at(index);
            socket->write(
                QJsonDocument(
                    QJsonObject{
                        {"id", request.value("id")},
                        {"status", "ok"},
                        {"data", QJsonObject{{"song_id", request.value("params").toObject().value("song_id")},
                                             {"custom_title", title},
                                             {"artist", ""},
                                             {"tags", QJsonArray{}}}}})
                    .toJson(QJsonDocument::Compact) +
                '\n');
            socket->flush();
        };
        controller.suggest(1, {});
        QVERIFY(controller.generating());
        QTRY_COMPARE(requests.size(), 1);
        controller.discardSuggestion();
        controller.suggest(2, {});
        QTRY_COMPARE(requests.size(), 2);
        respond(0, "old song");
        QTest::qWait(30);
        QCOMPARE(ready.size(), 0);
        QVERIFY(controller.generating());
        respond(1, "new song");
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.first().first().toMap().value("custom_title").toString(), QString("new song"));
        QVERIFY(!controller.generating());
        controller.suggest(2, {});
        QTRY_COMPARE(requests.size(), 3);
        controller.discardSuggestion();
        controller.suggest(2, {});
        QTRY_COMPARE(requests.size(), 4);
        respond(2, "same song, closed editor");
        QTest::qWait(30);
        QCOMPARE(ready.size(), 1);
        QVERIFY(controller.generating());
        socket->disconnectFromServer();
        QTRY_VERIFY(!controller.generating());
        QVERIFY(!controller.suggestionError().isEmpty());
        controller.discardSuggestion();
        QVERIFY(controller.suggestionError().isEmpty());
    }
    void ipcPreviewDoesNotSaveAndPlaybackRemainsResponsive() {
        HttpFixture server;
        server.responses.append({200, completion(validSuggestion), 250});
        BackendRuntime runtime;
        QVERIFY(runtime.start());
        Peer peer;
        peer.socket.connectToServer(runtime.serverName());
        QVERIFY(peer.socket.waitForConnected(3000));
        const auto configured =
            peer.call("ai.config.set", {{"base_url", server.baseUrl()}, {"model", "model"}});
        QCOMPARE(configured.value("status").toString(), QString("ok"));
        const auto path = m_directory.filePath("author - song.mp3");
        QFile audio(path);
        QVERIFY(audio.open(QIODevice::WriteOnly));
        audio.write("test-audio-bytes");
        audio.close();
        const auto imported = peer.call("library.import", {{"path", path}});
        const int songId = imported.value("data").toObject().value("song_id").toInt();
        QVERIFY(songId > 0);
        QCOMPARE(peer.call("song.update_metadata", {{"song_id", songId},
                                                    {"custom_title", "old"},
                                                    {"artist", "before"},
                                                    {"lyrics", "Keep lyrics"},
                                                    {"tags", QJsonArray{"收藏"}}})
                     .value("status")
                     .toString(),
                 QString("ok"));
        const int suggestionId =
            peer.send("song.suggest_metadata",
                      {{"song_id", songId},
                       {"draft", QJsonObject{{"custom_title", "draft"}, {"tags", QJsonArray{"收藏"}}}}});
        QTRY_COMPARE(server.requests.size(), 1);
        QElapsedTimer responseTime;
        responseTime.start();
        QCOMPARE(peer.call("player.set_volume", {{"volume", .3}}).value("status").toString(), QString("ok"));
        QVERIFY(responseTime.elapsed() < 200);
        const auto suggestion = peer.receive(suggestionId);
        QCOMPARE(suggestion.value("status").toString(), QString("ok"));
        auto metadata = peer.call("song.metadata", {{"song_id", songId}}).value("data").toObject();
        QCOMPARE(metadata.value("custom_title").toString(), QString("old"));
        QCOMPARE(metadata.value("artist").toString(), QString("before"));
        QCOMPARE(metadata.value("tags").toArray().size(), 1);
        const auto filled = suggestion.value("data").toObject();
        auto tags = filled.value("tags").toArray();
        tags.append("收藏");
        QCOMPARE(peer.call("song.update_metadata", {{"song_id", songId},
                                                    {"custom_title", filled.value("custom_title")},
                                                    {"artist", filled.value("artist")},
                                                    {"tags", tags}})
                     .value("status")
                     .toString(),
                 QString("ok"));
        peer.socket.abort();
        runtime.stop();
        QVERIFY(runtime.start());
        Peer reopened;
        reopened.socket.connectToServer(runtime.serverName());
        QVERIFY(reopened.socket.waitForConnected(3000));
        metadata = reopened.call("song.metadata", {{"song_id", songId}}).value("data").toObject();
        QCOMPARE(metadata.value("custom_title").toString(), QString("夜空"));
        QCOMPARE(metadata.value("lyrics").toString(), QString("Keep lyrics"));
        QCOMPARE(metadata.value("tags").toArray().size(), 3);
        QCOMPARE(reopened.call("ai.config.get")
                     .value("data")
                     .toObject()
                     .value("config")
                     .toObject()
                     .value("model")
                     .toString(),
                 QString("model"));
        QVERIFY(audio.open(QIODevice::ReadOnly));
        QCOMPARE(audio.readAll(), QByteArray("test-audio-bytes"));
        reopened.socket.abort();
        runtime.stop();
    }
};
QTEST_GUILESS_MAIN(AiTest)
#include "ai_test.moc"
