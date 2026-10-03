#include "ipc/serialization/kugou_serialization.h"
#include "infrastructure/kugou/kugou_music_service.h"
#include "infrastructure/library/music_directory.h"
#include "support/store_fixture.h"
#include "domain/lyrics/krc_parser.h"
#include "infrastructure/lyrics/kugou_provider.h"
#include "app_paths.h"
#include <QLockFile>

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <QtTest/QtTest>

#include <cstring>

using namespace nekotune;

namespace {
constexpr auto kHash = "0123456789abcdef0123456789abcdef";

struct Response {
    QByteArray body;
    int status = 200;
    QString mime = QStringLiteral("application/json");
    QUrl redirect;
    bool hang = false;
    qint64 contentLength = -1;
};

class FakeReply final : public QNetworkReply {
  public:
    FakeReply(const QNetworkRequest &request, const Response &response, QObject *parent)
        : QNetworkReply(parent), m_body(response.body) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        setHeader(QNetworkRequest::ContentTypeHeader, response.mime);
        if (response.contentLength >= 0)
            setHeader(QNetworkRequest::ContentLengthHeader, response.contentLength);
        if (!response.redirect.isEmpty())
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, response.redirect);
        open(QIODevice::ReadOnly);
        if (response.hang && !m_body.isEmpty())
            QTimer::singleShot(0, this, [this]() {
                if (isFinished())
                    return;
                emit readyRead();
                emit downloadProgress(m_body.size(), -1);
            });
        if (!response.hang)
            QTimer::singleShot(0, this, [this]() {
                if (isFinished())
                    return;
                emit readyRead();
                emit downloadProgress(m_body.size(), m_body.size());
                setFinished(true);
                emit finished();
            });
    }

    void abort() override {
        if (isFinished())
            return;
        setError(OperationCanceledError, QStringLiteral("Canceled"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

  protected:
    qint64 readData(char *data, qint64 maximum) override {
        const auto count = qMin(maximum, static_cast<qint64>(m_body.size() - m_offset));
        if (count <= 0)
            return -1;
        std::memcpy(data, m_body.constData() + m_offset, count);
        m_offset += count;
        return count;
    }

  private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class FakeManager final : public QNetworkAccessManager {
  public:
    QHash<QString, Response> routes;
    QHash<QString, int> calls;
    QHash<QString, QUrl> requestedUrls;
    QHash<QString, QByteArray> accountHeaders;
    QHash<QString, Operation> operations;
    QHash<QString, QJsonObject> requestBodies;

  protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *body) override {
        const auto route = request.url().path();
        ++calls[route];
        requestedUrls.insert(route, request.url());
        accountHeaders.insert(route, request.rawHeader("X-Account-Key"));
        operations.insert(route, operation);
        requestBodies.insert(route, body ? QJsonDocument::fromJson(body->readAll()).object() : QJsonObject{});
        return new FakeReply(request, routes.value(route, Response{R"({"status":404})", 404}), this);
    }
};

QByteArray json(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }

QJsonObject searchBody() {
    return {{QStringLiteral("status"), 1},
            {QStringLiteral("error_code"), 0},
            {QStringLiteral("data"),
             QJsonObject{{QStringLiteral("lists"),
                          QJsonArray{QJsonObject{{QStringLiteral("FileHash"), QString::fromLatin1(kHash)},
                                                 {QStringLiteral("SongName"), QStringLiteral("Song")},
                                                 {QStringLiteral("SingerName"), QStringLiteral("Singer")},
                                                 {QStringLiteral("Duration"), 120},
                                                 {QStringLiteral("MixSongID"), 42}}}}}}};
}

void configureSearch(FakeManager &manager) {
    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/search"), {json(searchBody())});
}

void configureSearchWithCover(FakeManager &manager, const QString &coverUrl) {
    auto body = searchBody();
    auto data = body.value(QStringLiteral("data")).toObject();
    auto rows = data.value(QStringLiteral("lists")).toArray();
    auto song = rows.at(0).toObject();
    song.insert(QStringLiteral("Image"), coverUrl);
    rows[0] = song;
    data.insert(QStringLiteral("lists"), rows);
    body.insert(QStringLiteral("data"), data);
    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/search"), {json(body)});
}

bool hasEvent(const QSignalSpy &spy, const QString &name) {
    for (const auto &row : spy)
        if (nekotune::toJson(row.at(0).value<nekotune::KugouEvent>())
                .value(QStringLiteral("event"))
                .toString() == name)
            return true;
    return false;
}
} // namespace

class KugouMusicTest final : public QObject {
    Q_OBJECT
    QTemporaryDir m_profile;
  private slots:
    void initTestCase() {
        QVERIFY(m_profile.isValid());
        qputenv("XDG_CONFIG_HOME", m_profile.filePath("config").toUtf8());
        qputenv("NEKOTUNE_HOME", m_profile.filePath("music").toUtf8());
        qputenv("XDG_DATA_HOME", m_profile.filePath("data").toUtf8());
        qunsetenv("KUGOU_ACCOUNT_API_KEY_FILE");
    }
    void init() {
        QVERIFY(AppPaths::saveSetting("kugou", QJsonObject{{"enabled", true}, {"worker_url", "https://worker.example"}}));
    }
    void configurationDefaultsPersistenceAndRequestGating();
    void migratesLegacyWorkerOrigin_data();
    void migratesLegacyWorkerOrigin();
    void rejectsInsecureWorkerUrl();
    void storesAndClearsPrivateKey();
    void searchResultsCarryTrustedCovers();
    void loginDownloadAndReuseSession();
    void reportsSmsAndLoginFailure();
    void rejectsPermissionRedirectAndNonAudio();
    void followsTrustedRedirect();
    void cancelsAndSkipsAmbiguousLyrics();
    void downloadsCoverAndPreservesExistingFile();
    void rejectsInvalidCoverWithoutLosingAudio();
};

void KugouMusicTest::configurationDefaultsPersistenceAndRequestGating() {
    QVERIFY(AppPaths::saveSetting("kugou", QJsonObject{}));
    QVERIFY(AppPaths::saveSetting("language", "en"));
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    qputenv("KUGOU_MUSIC_API_URL", "https://ignored.example");
    QTemporaryDir dir;
    FakeManager manager;
    manager.routes.insert("/api/music/kugou/v1/search", {json(searchBody())});
    KugouMusicService service(nullptr, &manager, {}, dir.filePath("session"),
                              dir.filePath("music"), dir.filePath("key"));
    qunsetenv("KUGOU_MUSIC_API_URL");
    QVERIFY(!service.status().enabled);
    QVERIFY(!service.status().configured);
    QVERIFY(service.status().workerUrl.isEmpty());
    QVERIFY(!service.startSearch("Song", 1).isEmpty());
    QVERIFY(!service.startCodeRequest("13800138000").isEmpty());
    QVERIFY(!service.startLogin("13800138000", "123456").isEmpty());
    QVERIFY(!service.startDownload(QString(32, 'a')).isEmpty());
    KugouProvider lyrics(nullptr, &manager);
    QSignalSpy failures(&lyrics, &KugouProvider::failed);
    QSignalSpy candidates(&lyrics, &KugouProvider::completed);
    LyricsQuery query;
    query.title = "Song";
    lyrics.request(query, 1, true);
    QCOMPARE(failures.count(), 1);
    QVERIFY(manager.calls.isEmpty());
    for (const QString url : {"", "http://worker.example", "https://worker.example/api",
                              "https://luy-music-api.lyuy.workers.dev/api/music/kugou/v1",
                              "https://user:pass@worker.example", "https://worker.example?key=value",
                              "https://worker.example#fragment", "not a URL"}) {
        QVERIFY(!service.saveConfiguration(true, url).isEmpty());
        QVERIFY(!service.status().enabled);
        QVERIFY(AppPaths::setting("kugou").toObject().isEmpty());
    }
    QCOMPARE(service.saveConfiguration(true, " https://first.example/ "), QString());
    QVERIFY(service.status().enabled);
    QCOMPARE(service.status().workerUrl, QString("https://first.example"));
    QCOMPARE(AppPaths::setting("language").toString(), QString("en"));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QCOMPARE(service.startSearch("Song", 1), QString());
    QVERIFY(!service.saveConfiguration(false, "https://first.example").isEmpty());
    QTRY_VERIFY(hasEvent(events, "kugou.search_results"));
    QCOMPARE(manager.requestedUrls.value("/api/music/kugou/v1/search").host(), QString("first.example"));
    lyrics.request(query, 2, true);
    QTRY_COMPARE(candidates.count(), 1);
    const auto oldCandidates = candidates.first().at(1).value<QVector<LyricsCandidate>>();
    QVERIFY(!oldCandidates.isEmpty());
    QCOMPARE(service.saveConfiguration(true, "https://second.example"), QString());
    lyrics.choose(oldCandidates.first(), 3);
    QCOMPARE(failures.count(), 2);
    QCOMPARE(service.startSearch("Song", 1), QString());
    QTRY_VERIFY(!service.status().busy);
    QCOMPARE(manager.requestedUrls.value("/api/music/kugou/v1/search").host(), QString("second.example"));
    lyrics.request(query, 4, true);
    QTRY_COMPARE(candidates.count(), 2);
    QCOMPARE(manager.requestedUrls.value("/api/music/kugou/v1/search").host(), QString("second.example"));
    QCOMPARE(service.saveConfiguration(false, "https://second.example"), QString());
    const auto calls = manager.calls;
    QVERIFY(!service.startSearch("Song", 1).isEmpty());
    lyrics.request(query, 5, true);
    lyrics.choose(oldCandidates.first(), 6);
    QCOMPARE(failures.count(), 4);
    QCOMPARE(manager.calls, calls);
    {
        KugouMusicService restored(nullptr, &manager, {}, dir.filePath("session"),
                                   dir.filePath("music"), dir.filePath("key"));
        QVERIFY(!restored.status().enabled);
        QCOMPARE(restored.status().workerUrl, QString("https://second.example"));
        QLockFile lock(AppPaths::configFile("settings.lock"));
        QVERIFY(lock.tryLock());
        QVERIFY(!restored.saveConfiguration(true, "https://third.example").isEmpty());
        QVERIFY(!restored.status().enabled);
        QCOMPARE(restored.status().workerUrl, QString("https://second.example"));
    }
    QCOMPARE(AppPaths::setting("kugou").toObject().value("worker_url").toString(),
             QString("https://second.example"));
    QCOMPARE(service.saveConfiguration(false, ""), QString());
    QVERIFY(service.status().workerUrl.isEmpty());
}

void KugouMusicTest::migratesLegacyWorkerOrigin_data() {
    QTest::addColumn<QString>("origin");
    QTest::addColumn<QString>("expectedOrigin");
    QTest::addColumn<bool>("enabled");
    QTest::newRow("legacy") << QStringLiteral("https://kugou-lyrics-api.lyuy.workers.dev")
                            << QStringLiteral("https://luy-music-api.lyuy.workers.dev") << true;
    QTest::newRow("legacy-trailing-slash") << QStringLiteral("https://kugou-lyrics-api.lyuy.workers.dev/")
                                           << QStringLiteral("https://luy-music-api.lyuy.workers.dev") << true;
    QTest::newRow("legacy-disabled") << QStringLiteral("https://kugou-lyrics-api.lyuy.workers.dev")
                                     << QStringLiteral("https://luy-music-api.lyuy.workers.dev") << false;
    QTest::newRow("new-trailing-slash") << QStringLiteral("https://luy-music-api.lyuy.workers.dev/")
                                        << QStringLiteral("https://luy-music-api.lyuy.workers.dev") << true;
    QTest::newRow("custom") << QStringLiteral("https://custom.example:8443/") << QStringLiteral("https://custom.example:8443") << true;
    QTest::newRow("loopback") << QStringLiteral("http://127.0.0.1:8787/") << QStringLiteral("http://127.0.0.1:8787") << true;
}

void KugouMusicTest::migratesLegacyWorkerOrigin() {
    QFETCH(QString, origin);
    QFETCH(QString, expectedOrigin);
    QFETCH(bool, enabled);
    const QJsonObject stored{{"enabled", enabled}, {"worker_url", origin}};
    QVERIFY(AppPaths::saveSetting("kugou", stored));
    QTemporaryDir dir;
    FakeManager manager;
    configureSearch(manager);
    KugouMusicService service(nullptr, &manager, {}, dir.filePath("session"),
                              dir.filePath("music"), dir.filePath("key"));
    QCOMPARE(service.status().workerUrl, expectedOrigin);
    QCOMPARE(service.status().enabled, enabled);
    QCOMPARE(AppPaths::setting("kugou").toObject(), stored);

    KugouProvider lyrics(nullptr, &manager);
    QSignalSpy candidates(&lyrics, &KugouProvider::completed);
    QSignalSpy failures(&lyrics, &KugouProvider::failed);
    LyricsQuery query;
    query.title = QStringLiteral("晴天 & +");
    if (enabled) {
        QSignalSpy events(&service, &KugouMusicService::eventReady);
        QCOMPARE(service.startSearch(query.title, 2), QString());
        QTRY_VERIFY(hasEvent(events, "kugou.search_results"));
        const auto musicUrl = manager.requestedUrls.value("/api/music/kugou/v1/search");
        QCOMPARE(musicUrl.adjusted(QUrl::RemoveQuery), QUrl(expectedOrigin + "/api/music/kugou/v1/search"));
        QCOMPARE(QUrlQuery(musicUrl).queryItemValue("keywords", QUrl::FullyDecoded), query.title);
        QCOMPARE(QUrlQuery(musicUrl).queryItemValue("page"), QString("2"));
        lyrics.request(query, 1, true);
        QTRY_COMPARE(candidates.count(), 1);
        const auto lyricsUrl = manager.requestedUrls.value("/api/music/kugou/v1/search");
        QCOMPARE(lyricsUrl.adjusted(QUrl::RemoveQuery), QUrl(expectedOrigin + "/api/music/kugou/v1/search"));
        QCOMPARE(QUrlQuery(lyricsUrl).queryItemValue("keywords", QUrl::FullyDecoded), query.title);
        QCOMPARE(manager.calls.value("/api/music/kugou/v1/search"), 2);
        QCOMPARE(manager.operations.value("/api/music/kugou/v1/search"), QNetworkAccessManager::GetOperation);
        QVERIFY(manager.accountHeaders.value("/api/music/kugou/v1/search").isEmpty());
    } else {
        QVERIFY(!service.startSearch(query.title, 1).isEmpty());
        lyrics.request(query, 1, true);
        QCOMPARE(failures.count(), 1);
        QVERIFY(manager.calls.isEmpty());
    }
    QCOMPARE(service.saveConfiguration(enabled, origin), QString());
    QCOMPARE(AppPaths::setting("kugou").toObject(),
             (QJsonObject{{"enabled", enabled}, {"worker_url", expectedOrigin}}));
}

// Provider artwork is accepted only from the configured trusted image hosts.
// HTTP image hints are normalized to HTTPS and the size placeholder is resolved.
// An unsafe result cover may fall back to trusted group artwork.
// The fallback must not preserve an arbitrary URL from the rejected candidate.
// This uses fake search responses and makes no claim about live account access.
void KugouMusicTest::searchResultsCarryTrustedCovers() {
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    FakeManager manager;
    auto body = searchBody();
    auto data = body.value("data").toObject();
    auto rows = data.value("lists").toArray();
    auto parent = rows.first().toObject();
    parent.insert("Image", "http://imge.kugou.com/stdmusic/{size}/parent.jpg");
    parent.insert("Grp", QJsonArray{
                             QJsonObject{{"FileHash", QString(32, 'b')},
                                         {"Image", "https://imge.kugou.com/stdmusic/{size}/variant.jpg"}},
                             QJsonObject{{"FileHash", QString(32, 'c')}},
                             QJsonObject{{"FileHash", QString(32, 'd')},
                                         {"Image", "https://untrusted.example/cover.jpg"}}});
    rows[0] = parent;
    rows.append(QJsonObject{{"FileHash", QString(32, 'e')}, {"SongName", "No cover"}});
    data.insert("lists", rows);
    body.insert("data", data);
    manager.routes.insert("/api/music/kugou/v1/search", {json(body)});
    KugouMusicService service(nullptr, &manager, QUrl("https://worker.example"),
                              dir.filePath("session.json"), dir.filePath("Music"));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QCOMPARE(service.startSearch("Song", 1), QString());
    QTRY_VERIFY(hasEvent(events, "kugou.search_results"));
    const auto result = nekotune::toJson(events.last().at(0).value<KugouEvent>()).value("songs").toArray();
    QCOMPARE(result.size(), 5);
    QCOMPARE(result[0].toObject().value("cover_url").toString(),
             QString("https://imge.kugou.com/stdmusic/240/parent.jpg"));
    QCOMPARE(result[1].toObject().value("cover_url").toString(),
             QString("https://imge.kugou.com/stdmusic/240/variant.jpg"));
    QCOMPARE(result[2].toObject().value("cover_url").toString(),
             result[0].toObject().value("cover_url").toString());
    QCOMPARE(result[3].toObject().value("cover_url").toString(), QString());
    QCOMPARE(result[4].toObject().value("cover_url").toString(), QString());
    QCOMPARE(manager.calls.size(), 1); // Searching does not download artwork in the backend.
}

// Remote account requests carry credentials and require an HTTPS worker endpoint.
// Loopback HTTP is a deliberate local-development exception.
// The fixture exercises configuration validation before any request can be sent.
// An invalid endpoint must report an error rather than silently downgrade transport.
void KugouMusicTest::rejectsInsecureWorkerUrl() {
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    FakeManager manager;
    KugouMusicService remote(nullptr, &manager, QUrl(QStringLiteral("http://worker.example")),
                             dir.filePath(QStringLiteral("remote.json")),
                             dir.filePath(QStringLiteral("Music")));
    QVERIFY(!nekotune::toJson(remote.status()).value(QStringLiteral("configured")).toBool());
    QVERIFY(!remote.startSearch(QStringLiteral("Song"), 1).isEmpty());
    QCOMPARE(manager.calls.size(), 0);
    KugouMusicService local(nullptr, &manager, QUrl(QStringLiteral("http://127.0.0.1:8787")),
                            dir.filePath(QStringLiteral("local.json")),
                            dir.filePath(QStringLiteral("Music")));
    QVERIFY(nekotune::toJson(local.status()).value(QStringLiteral("configured")).toBool());
}

// The saved key lives behind the credential-store interface, outside public status JSON.
// Key availability and the key bytes themselves are different API responsibilities.
// Saving and clearing must update status without disclosing the secret over IPC.
// The injected paths and store keep this lifecycle independent of user credentials.
// Clearing a saved key may reveal the configured fallback, not erase that fallback source.
void KugouMusicTest::storesAndClearsPrivateKey() {
    qputenv("KUGOU_ACCOUNT_API_KEY", "environment-key");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto keyPath = dir.filePath(QStringLiteral("config/kugou-account-key"));
    const auto sessionPath = dir.filePath(QStringLiteral("session.json"));
    const auto musicPath = dir.filePath(QStringLiteral("Music"));
    FakeManager manager;
    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/register/dev"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("cookies"),
                                  QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device")}}}})});
    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/captcha/sent"), {json({{QStringLiteral("status"), 1}})});
    {
        KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")),
                                  sessionPath, musicPath, keyPath);
        QVERIFY(nekotune::toJson(service.status()).value(QStringLiteral("configured")).toBool());
        QVERIFY(!nekotune::toJson(service.status()).value(QStringLiteral("key_saved")).toBool());
        QVERIFY(!service.saveAccountKey(QStringLiteral("\n")).isEmpty());
        QVERIFY(!service.saveAccountKey(QStringLiteral("part\nother")).isEmpty());
        QVERIFY(!service.saveAccountKey(QString(4097, QLatin1Char('x'))).isEmpty());
        QVERIFY(!service.saveAccountKey(QString(2049, QChar(0x4e2d))).isEmpty());
        QVERIFY(!QFileInfo::exists(keyPath));
        QCOMPARE(service.saveAccountKey(QStringLiteral("saved-key")), QString());
        QVERIFY(nekotune::toJson(service.status()).value(QStringLiteral("key_saved")).toBool());
        QVERIFY(!QFileInfo::exists(keyPath));
        const auto stored = systemCredentialStore()->read(keyPath);
        QVERIFY(stored && stored.value());
        QCOMPARE(*stored.value(), QByteArray("saved-key"));
        QVERIFY(!QJsonDocument(nekotune::toJson(service.status())).toJson().contains("saved-key"));
    }
    {
        KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")),
                                  sessionPath, musicPath, keyPath);
        QVERIFY(nekotune::toJson(service.status()).value(QStringLiteral("key_saved")).toBool());
        QSignalSpy events(&service, &KugouMusicService::eventReady);
        QCOMPARE(service.startCodeRequest(QStringLiteral("13800138000")), QString());
        QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.code_sent")));
        QCOMPARE(manager.accountHeaders.value(QStringLiteral("/api/music/kugou/v1/register/dev")), QByteArray("saved-key"));
        QCOMPARE(service.clearAccountKey(), QString());
        QVERIFY(!QFileInfo::exists(keyPath));
        QVERIFY(!nekotune::toJson(service.status()).value(QStringLiteral("key_saved")).toBool());
        QVERIFY(nekotune::toJson(service.status()).value(QStringLiteral("configured")).toBool());
        QCOMPARE(service.startCodeRequest(QStringLiteral("13800138000")), QString());
        QTRY_COMPARE(manager.calls.value(QStringLiteral("/api/music/kugou/v1/captcha/sent")), 2);
        QCOMPARE(manager.accountHeaders.value(QStringLiteral("/api/music/kugou/v1/captcha/sent")),
                 QByteArray("environment-key"));
    }
}

// The fake service drives SMS registration, login and audio resolution as separate steps.
// Restoring the session avoids requiring another login after the service is recreated.
// A completed audio file can be reused rather than downloaded repeatedly.
// Lyrics enrichment follows audio completion and reports its own outcome.
// The requested quality is checked at the URL resolution boundary.
// These assertions cover protocol handling, not real paid-account entitlement.
void KugouMusicTest::loginDownloadAndReuseSession() {
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    FakeManager manager;
    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/register/dev"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("cookies"),
                                  QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device")}}}})});
    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/captcha/sent"), {json({{QStringLiteral("status"), 1}})});
    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/login/cellphone"),
        {json(
            {{QStringLiteral("status"), 1},
             {QStringLiteral("data"), QJsonObject{{QStringLiteral("token"), QStringLiteral("token")},
                                                  {QStringLiteral("userid"), QStringLiteral("123")}}},
             {QStringLiteral("cookies"), QJsonObject{{QStringLiteral("token"), QStringLiteral("token")},
                                                     {QStringLiteral("userid"), QStringLiteral("123")}}}})});
    configureSearch(manager);
    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/song/url"),
        {json({{QStringLiteral("status"), 1},
               {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(QStringLiteral("/song.mp3"), {"ID3test-audio", 200, QStringLiteral("audio/mpeg")});
    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/search/lyric"),
        {json({{QStringLiteral("status"), 200},
               {QStringLiteral("candidates"),
                QJsonArray{QJsonObject{{QStringLiteral("song"), QStringLiteral("Song")},
                                       {QStringLiteral("singer"), QStringLiteral("Singer")},
                                       {QStringLiteral("duration"), 120000},
                                       {QStringLiteral("id"), 12},
                                       {QStringLiteral("accesskey"), QStringLiteral("abc")}}}}})});
    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/lyric"),
                          {json({{QStringLiteral("decodeContent"), QStringLiteral("[00:01.00]Hello\n")}})});
    const auto binaryKrc =
        QByteArray::fromBase64("a3JjMTjb6lmXhn4OfBi4yEQcISJrCUzGbAZERyO2laxA9/"
                               "4AI+cCEzSSFye2p6u+XX0OCkJTVkFeO6zTtiHUiRajK3JZRphRTodSJ3jhjTng0hPUV+Y=");
    manager.routes.insert(QStringLiteral("/download"),
                          {json({{QStringLiteral("status"), 200},
                                 {QStringLiteral("content"), QString::fromLatin1(binaryKrc.toBase64())}})});
    const auto session = dir.filePath(QStringLiteral("session.json"));
    const auto music = dir.filePath(QStringLiteral("Music"));
    StoreFixture downloadStore(dir.filePath("downloads.sqlite3"));
    QVERIFY(downloadStore.isReady());
    MusicDirectory directory(downloadStore.db, music);
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")), session,
                              music, {}, [&directory](const QString &hash, const QString &title) {
                                  return directory.reserveDownload(hash, title);
                              });
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QSignalSpy audio(&service, &KugouMusicService::audioReady);
    QCOMPARE(nekotune::toJson(service.status()).value(QStringLiteral("logged_in")).toBool(), false);
    QCOMPARE(service.startCodeRequest(QStringLiteral("13800138000")), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.code_sent")));
    QCOMPARE(manager.calls.value(QStringLiteral("/api/music/kugou/v1/register/dev")), 1);
    QCOMPARE(manager.calls.value(QStringLiteral("/api/music/kugou/v1/captcha/sent")), 1);
    QCOMPARE(manager.requestBodies.value("/api/music/kugou/v1/captcha/sent").value("mobile").toString(),
             QString("13800138000"));
    QCOMPARE(manager.requestBodies.value("/api/music/kugou/v1/captcha/sent")
                 .value("cookies").toObject().value("dfid").toString(), QString("device"));
    QVERIFY(!QFileInfo::exists(session));
    const auto persisted = systemCredentialStore()->read(session);
    QVERIFY(persisted && persisted.value());

    events.clear();
    QCOMPARE(service.startLogin(QStringLiteral("13800138000"), QStringLiteral("123456")), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.logged_in")));
    QVERIFY(nekotune::toJson(service.status()).value(QStringLiteral("logged_in")).toBool());
    events.clear();
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 2), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.search_results")));
    QCOMPARE(nekotune::toJson(events.last().at(0).value<nekotune::KugouEvent>())
                 .value(QStringLiteral("page"))
                 .toInt(),
             2);
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 1);
    const auto path = audio.at(0).at(0).toString();
    for (const QString route : {"/api/music/kugou/v1/register/dev", "/api/music/kugou/v1/captcha/sent",
                                 "/api/music/kugou/v1/login/cellphone", "/api/music/kugou/v1/song/url"}) {
        QCOMPARE(manager.requestedUrls.value(route), QUrl("https://worker.example" + route));
        QCOMPARE(manager.operations.value(route), QNetworkAccessManager::PostOperation);
        QCOMPARE(manager.accountHeaders.value(route), QByteArray("test-key"));
        QVERIFY(manager.requestBodies.value(route).value("cookies").isObject());
    }
    const auto songBody = manager.requestBodies.value("/api/music/kugou/v1/song/url");
    QCOMPARE(songBody.value("cookies").toObject().value("token").toString(), QString("token"));
    QCOMPARE(songBody.value("cookies").toObject().value("userid").toString(), QString("123"));
    const auto lyricSearch = manager.requestedUrls.value("/api/music/kugou/v1/search/lyric");
    QCOMPARE(QUrlQuery(lyricSearch).queryItemValue("hash"), QString::fromLatin1(kHash));
    QCOMPARE(QUrlQuery(lyricSearch).queryItemValue("duration"), QString("120000"));
    QVERIFY(manager.accountHeaders.value("/api/music/kugou/v1/search/lyric").isEmpty());
    QCOMPARE(manager.requestedUrls.value("/download").adjusted(QUrl::RemoveQuery),
             QUrl("https://lyrics.kugou.com/download"));
    QVERIFY(manager.accountHeaders.value("/download").isEmpty());
    QCOMPARE(path, QDir(music).filePath("000001/000001.mp3"));
    QCOMPARE(audio.at(0).at(1).toString(), QStringLiteral("saved"));
    QVERIFY(QFileInfo(path).isFile());
    const auto lrcPath = QFileInfo(path).absolutePath() + QLatin1Char('/') +
                         QFileInfo(path).completeBaseName() + QStringLiteral(".lrc");
    QVERIFY(QFileInfo(lrcPath).isFile());
    const auto krcPath = QFileInfo(path).absolutePath() + QLatin1Char('/') +
                         QFileInfo(path).completeBaseName() + QStringLiteral(".krc");
    QVERIFY(QFileInfo(krcPath).isFile());
    QFile krc(krcPath);
    QVERIFY(krc.open(QIODevice::ReadOnly));
    QCOMPARE(krc.readAll(), binaryKrc);
    QVERIFY(KrcParser::decode(binaryKrc).has_value());
    QCOMPARE(manager.calls.value(QStringLiteral("/download")), 1);
    QFile lrc(lrcPath);
    QVERIFY(lrc.open(QIODevice::WriteOnly | QIODevice::Truncate));
    lrc.write("custom");
    lrc.close();
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 2);
    QCOMPARE(audio.at(1).at(1).toString(), QStringLiteral("existing"));
    QCOMPARE(manager.calls.value(QStringLiteral("/song.mp3")), 1);
    QVERIFY(lrc.open(QIODevice::ReadOnly));
    QCOMPARE(lrc.readAll(), QByteArray("custom"));
    KugouMusicService resumed(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")), session,
                              music);
    QVERIFY(nekotune::toJson(resumed.status()).value(QStringLiteral("logged_in")).toBool());
}

// Audio resolution must stop when account entitlement denies the requested resource.
// Redirects outside the trusted audio hosts cannot receive a follow-up request.
// An HTML error page is not a completed audio download even with a successful status.
// Oversized content must be rejected before publishing the destination file.
// Each failure leaves no usable audio result or misleading completion signal.
// The test covers independent trust, content and size checks in the download pipeline.
void KugouMusicTest::rejectsPermissionRedirectAndNonAudio() {
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    const auto session = dir.filePath(QStringLiteral("session.json"));
    QFile file(session);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        json({{QStringLiteral("version"), 1},
              {QStringLiteral("cookies"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device")},
                                                      {QStringLiteral("token"), QStringLiteral("token")},
                                                      {QStringLiteral("userid"), QStringLiteral("123")}}}}));
    file.close();
    FakeManager manager;
    configureSearch(manager);
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")), session,
                              dir.filePath(QStringLiteral("Music")));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.search_results")));

    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/song/url"),
                          {json({{QStringLiteral("status"), 0},
                                 {QStringLiteral("priv_status"), 0},
                                 {QStringLiteral("fail_process"), QJsonArray{QStringLiteral("pkg")}}})});
    events.clear();
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));

    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/song/url"),
        {json({{QStringLiteral("status"), 1},
               {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(
        QStringLiteral("/song.mp3"),
        {"", 302, QStringLiteral("audio/mpeg"), QUrl(QStringLiteral("https://evil.example/song.mp3"))});
    events.clear();
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));
    QCOMPARE(manager.calls.value(QStringLiteral("/song.mp3")), 1);

    manager.routes.insert(QStringLiteral("/song.mp3"),
                          {"<html>bad</html>", 200, QStringLiteral("text/html")});
    events.clear();
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));

    manager.routes.insert(QStringLiteral("/song.mp3"),
                          {"ID3", 200, QStringLiteral("audio/mpeg"), {}, false, 101LL * 1024 * 1024});
    events.clear();
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));
    QVERIFY(QDir(dir.filePath(QStringLiteral("Music"))).entryList(QDir::Files).isEmpty());
}

// An HTTP success can still contain a provider-level authentication failure.
// SMS and login errors must retain their specific business error information.
// A failed login cannot change the session into an authenticated state.
// The fake responses isolate this distinction from network transport failures.
void KugouMusicTest::reportsSmsAndLoginFailure() {
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    FakeManager manager;
    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/register/dev"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("cookies"),
                                  QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device")}}}})});
    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/captcha/sent"),
                          {json({{QStringLiteral("status"), 0}, {QStringLiteral("error_code"), 400}})});
    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/login/cellphone"),
                          {json({{QStringLiteral("status"), 0}, {QStringLiteral("error_code"), 20028}})});
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")),
                              dir.filePath(QStringLiteral("session.json")),
                              dir.filePath(QStringLiteral("Music")));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QCOMPARE(service.startCodeRequest(QStringLiteral("13800138000")), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));
    events.clear();
    QCOMPARE(service.startLogin(QStringLiteral("13800138000"), QStringLiteral("123456")), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));
    QVERIFY(!nekotune::toJson(service.status()).value(QStringLiteral("logged_in")).toBool());
}

// A redirect inside the allowed HTTPS audio host family may continue the download.
// The final validated audio file should be published exactly once.
// Missing optional lyrics must not roll back that completed audio file.
// This distinguishes an allowed redirect from the hostile redirects in the rejection fixture.
void KugouMusicTest::followsTrustedRedirect() {
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    const auto session = dir.filePath(QStringLiteral("session.json"));
    QFile file(session);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        json({{QStringLiteral("version"), 1},
              {QStringLiteral("cookies"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device")},
                                                      {QStringLiteral("token"), QStringLiteral("token")},
                                                      {QStringLiteral("userid"), QStringLiteral("123")}}}}));
    file.close();
    FakeManager manager;
    configureSearch(manager);
    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/song/url"),
        {json({{QStringLiteral("status"), 200},
               {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(
        QStringLiteral("/song.mp3"),
        {"", 302, QStringLiteral("audio/mpeg"), QUrl(QStringLiteral("https://cdn.kugou.com/final.mp3"))});
    manager.routes.insert(QStringLiteral("/final.mp3"), {"ID3test", 200, QStringLiteral("audio/mpeg")});
    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/search/lyric"),
        {json({{QStringLiteral("status"), 404}, {QStringLiteral("candidates"), QJsonArray{}}})});
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")), session,
                              dir.filePath(QStringLiteral("Music")));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QSignalSpy audio(&service, &KugouMusicService::audioReady);
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.search_results")));
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 1);
    QCOMPARE(audio.at(0).at(1).toString(), QStringLiteral("none"));
    QCOMPARE(manager.calls.value(QStringLiteral("/final.mp3")), 1);
}

// Cancellation before audio completes must remove partial output and suppress audioReady.
// Equally plausible lyric candidates remain uncertain rather than being chosen arbitrarily.
// Cancellation during later lyric enrichment must retain the already completed audio.
// A cancelled KRC request must not publish KRC bytes or start an unintended LRC retry.
// The completion status therefore depends on which pipeline phase owned the cancellation.
// Controlled hanging replies make these timing boundaries deterministic.
void KugouMusicTest::cancelsAndSkipsAmbiguousLyrics() {
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    const auto session = dir.filePath(QStringLiteral("session.json"));
    QFile file(session);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        json({{QStringLiteral("version"), 1},
              {QStringLiteral("cookies"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device")},
                                                      {QStringLiteral("token"), QStringLiteral("token")},
                                                      {QStringLiteral("userid"), QStringLiteral("123")}}}}));
    file.close();
    FakeManager manager;
    configureSearch(manager);
    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/song/url"),
        {json({{QStringLiteral("status"), 1},
               {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(QStringLiteral("/song.mp3"),
                          {"ID3partial", 200, QStringLiteral("audio/mpeg"), {}, true});
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")), session,
                              dir.filePath(QStringLiteral("Music")));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QSignalSpy audio(&service, &KugouMusicService::audioReady);
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.search_results")));
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(manager.calls.value(QStringLiteral("/song.mp3")), 1);
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.download_progress")));
    QCOMPARE(service.cancelDownload(), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.download_cancelled")));
    QCOMPARE(audio.count(), 0);
    QVERIFY(QDir(dir.filePath(QStringLiteral("Music"))).entryList(QDir::Files | QDir::Hidden).isEmpty());

    manager.routes.insert(QStringLiteral("/song.mp3"), {"ID3test", 200, QStringLiteral("audio/mpeg")});
    const QJsonObject candidate{{QStringLiteral("song"), QStringLiteral("Song")},
                                {QStringLiteral("singer"), QStringLiteral("Singer")},
                                {QStringLiteral("duration"), 120000},
                                {QStringLiteral("id"), 1},
                                {QStringLiteral("accesskey"), QStringLiteral("abc")}};
    manager.routes.insert(QStringLiteral("/api/music/kugou/v1/search/lyric"),
                          {json({{QStringLiteral("status"), 200},
                                 {QStringLiteral("candidates"), QJsonArray{candidate, candidate}}})});
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 1);
    QCOMPARE(audio.at(0).at(1).toString(), QStringLiteral("uncertain"));
    QCOMPARE(manager.calls.value(QStringLiteral("/api/music/kugou/v1/lyric")), 0);

    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/search/lyric"),
        {json({{QStringLiteral("status"), 200}, {QStringLiteral("candidates"), QJsonArray{candidate}}})});
    manager.routes.insert(
        QStringLiteral("/download"),
        {R"({"status":200,"content":"pending"})", 200, QStringLiteral("application/json"), {}, true});
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(manager.calls.value(QStringLiteral("/download")), 1);
    QCOMPARE(service.cancelDownload(), QString());
    QTRY_COMPARE(audio.count(), 2);
    QCOMPARE(audio.at(1).at(1).toString(), QStringLiteral("skipped"));
    QCOMPARE(manager.calls.value(QStringLiteral("/api/music/kugou/v1/lyric")), 0);
    const QFileInfo downloaded(audio.at(1).at(0).toString());
    QVERIFY(!QFileInfo::exists(downloaded.absolutePath() + QLatin1Char('/') + downloaded.completeBaseName() +
                               QStringLiteral(".krc")));
}

// Trusted artwork is decoded before it is installed beside the completed audio.
// The size placeholder is normalized before the image request is made.
// Reusing an existing local cover avoids both replacement and another network fetch.
// The image assertions check actual content handling rather than only a filename suffix.
// Audio completion remains independent of whether artwork was newly downloaded.
void KugouMusicTest::downloadsCoverAndPreservesExistingFile() {
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    const auto session = dir.filePath(QStringLiteral("session.json"));
    QFile file(session);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        json({{QStringLiteral("version"), 1},
              {QStringLiteral("cookies"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device")},
                                                      {QStringLiteral("token"), QStringLiteral("token")},
                                                      {QStringLiteral("userid"), QStringLiteral("123")}}}}));
    file.close();
    FakeManager manager;
    configureSearchWithCover(manager, QStringLiteral("http://imge.kugou.com/stdmusic/{size}/cover.jpg"));
    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/song/url"),
        {json({{QStringLiteral("status"), 1},
               {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(QStringLiteral("/song.mp3"), {"ID3audio", 200, QStringLiteral("audio/mpeg")});
    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/search/lyric"),
        {json({{QStringLiteral("status"), 404}, {QStringLiteral("candidates"), QJsonArray{}}})});
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "PNG"));
    manager.routes.insert(QStringLiteral("/stdmusic/240/cover.jpg"), {png, 200, QStringLiteral("image/png")});
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")), session,
                              dir.filePath(QStringLiteral("Music")));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QSignalSpy audio(&service, &KugouMusicService::audioReady);
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.search_results")));
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 1);
    QCOMPARE(audio.at(0).at(2).toString(), QStringLiteral("saved"));
    const auto downloaded = QFileInfo(audio.at(0).at(0).toString());
    const auto cover =
        downloaded.absolutePath() + QLatin1Char('/') + downloaded.completeBaseName() + QStringLiteral(".png");
    QVERIFY(QFileInfo(cover).isFile());
    QCOMPARE(manager.requestedUrls.value(QStringLiteral("/stdmusic/240/cover.jpg")).scheme(),
             QStringLiteral("https"));
    QFile existing(cover);
    QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
    existing.write("user-cover");
    existing.close();
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 2);
    QCOMPARE(audio.at(1).at(2).toString(), QStringLiteral("existing"));
    QCOMPARE(manager.calls.value(QStringLiteral("/stdmusic/240/cover.jpg")), 1);
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("user-cover"));
}

// Artwork is optional enrichment after the audio has already been committed.
// Unsafe redirects, invalid image bytes and excessive sizes report cover failure separately.
// None of these outcomes may erase a valid completed audio file.
// audioReady must still be emitted so the download can enter the local library.
// This protects useful partial success while rejecting untrusted sidecar content.
void KugouMusicTest::rejectsInvalidCoverWithoutLosingAudio() {
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    const auto session = dir.filePath(QStringLiteral("session.json"));
    QFile file(session);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        json({{QStringLiteral("version"), 1},
              {QStringLiteral("cookies"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device")},
                                                      {QStringLiteral("token"), QStringLiteral("token")},
                                                      {QStringLiteral("userid"), QStringLiteral("123")}}}}));
    file.close();
    FakeManager manager;
    configureSearchWithCover(manager, QStringLiteral("https://imge.kugou.com/stdmusic/{size}/bad.jpg"));
    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/song/url"),
        {json({{QStringLiteral("status"), 1},
               {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(QStringLiteral("/song.mp3"), {"ID3audio", 200, QStringLiteral("audio/mpeg")});
    manager.routes.insert(
        QStringLiteral("/api/music/kugou/v1/search/lyric"),
        {json({{QStringLiteral("status"), 404}, {QStringLiteral("candidates"), QJsonArray{}}})});
    manager.routes.insert(QStringLiteral("/stdmusic/240/bad.jpg"),
                          {"<html>not image</html>", 200, QStringLiteral("image/jpeg")});
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")), session,
                              dir.filePath(QStringLiteral("Music")));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QSignalSpy audio(&service, &KugouMusicService::audioReady);
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.search_results")));
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 1);
    QCOMPARE(audio.at(0).at(2).toString(), QStringLiteral("error"));
    const auto downloaded = QFileInfo(audio.at(0).at(0).toString());
    QVERIFY(downloaded.isFile());
    QVERIFY(!QFileInfo(downloaded.absolutePath() + QLatin1Char('/') + downloaded.completeBaseName() +
                       QStringLiteral(".jpg"))
                 .exists());
    configureSearchWithCover(manager, QStringLiteral("https://evil.example/cover.jpg"));
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(manager.calls.value(QStringLiteral("/api/music/kugou/v1/search")) == 2);
    QTRY_VERIFY(!nekotune::toJson(service.status()).value(QStringLiteral("busy")).toBool());
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 2);
    QCOMPARE(audio.at(1).at(2).toString(), QStringLiteral("none"));
    QCOMPARE(manager.calls.value(QStringLiteral("/cover.jpg")), 0);
    QImage replacement(2, 2, QImage::Format_RGB32);
    replacement.fill(Qt::blue);
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(replacement.save(&buffer, "PNG"));
    configureSearchWithCover(manager, QStringLiteral("https://imge.kugou.com/stdmusic/{size}/bad.jpg"));
    manager.routes.insert(QStringLiteral("/stdmusic/240/bad.jpg"), {png, 200, QStringLiteral("image/png")});
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(manager.calls.value(QStringLiteral("/api/music/kugou/v1/search")) == 3);
    QTRY_VERIFY(!nekotune::toJson(service.status()).value(QStringLiteral("busy")).toBool());
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 3);
    QCOMPARE(audio.at(2).at(2).toString(), QStringLiteral("saved"));
    QCOMPARE(manager.calls.value(QStringLiteral("/song.mp3")), 1);
    QVERIFY(QFileInfo(downloaded.absolutePath() + QLatin1Char('/') + downloaded.completeBaseName() +
                      QStringLiteral(".png"))
                .isFile());
}

QTEST_GUILESS_MAIN(KugouMusicTest)
#include "kugou_music_test.moc"
