#include "kugou/kugou_music_service.h"
#include "lyrics/krc_parser.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>
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
        : QNetworkReply(parent), m_body(response.body)
    {
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
                if (isFinished()) return;
                emit readyRead();
                emit downloadProgress(m_body.size(), -1);
            });
        if (!response.hang)
            QTimer::singleShot(0, this, [this]() {
                if (isFinished()) return;
                emit readyRead();
                emit downloadProgress(m_body.size(), m_body.size());
                setFinished(true);
                emit finished();
            });
    }

    void abort() override
    {
        if (isFinished()) return;
        setError(OperationCanceledError, QStringLiteral("Canceled"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QNetworkReply::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto count = qMin(maximum, static_cast<qint64>(m_body.size() - m_offset));
        if (count <= 0) return -1;
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

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        const auto route = request.url().path();
        ++calls[route];
        requestedUrls.insert(route, request.url());
        accountHeaders.insert(route, request.rawHeader("X-Account-Key"));
        return new FakeReply(request, routes.value(route, Response{R"({"status":404})", 404}), this);
    }
};

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject searchBody()
{
    return {{QStringLiteral("status"), 1}, {QStringLiteral("error_code"), 0},
            {QStringLiteral("data"), QJsonObject{{QStringLiteral("lists"), QJsonArray{
                QJsonObject{{QStringLiteral("FileHash"), QString::fromLatin1(kHash)},
                            {QStringLiteral("SongName"), QStringLiteral("Song")},
                            {QStringLiteral("SingerName"), QStringLiteral("Singer")},
                            {QStringLiteral("Duration"), 120},
                            {QStringLiteral("MixSongID"), 42}}
            }}}}};
}

void configureSearch(FakeManager &manager)
{
    manager.routes.insert(QStringLiteral("/search"), {json(searchBody())});
}

void configureSearchWithCover(FakeManager &manager, const QString &coverUrl)
{
    auto body = searchBody();
    auto data = body.value(QStringLiteral("data")).toObject();
    auto rows = data.value(QStringLiteral("lists")).toArray();
    auto song = rows.at(0).toObject();
    song.insert(QStringLiteral("Image"), coverUrl);
    rows[0] = song;
    data.insert(QStringLiteral("lists"), rows);
    body.insert(QStringLiteral("data"), data);
    manager.routes.insert(QStringLiteral("/search"), {json(body)});
}

bool hasEvent(const QSignalSpy &spy, const QString &name)
{
    for (const auto &row : spy)
        if (row.at(0).value<QJsonObject>().value(QStringLiteral("event")).toString() == name) return true;
    return false;
}
} // namespace

class KugouMusicTest final : public QObject {
    Q_OBJECT
private slots:
    void rejectsInsecureWorkerUrl();
    void storesAndClearsPrivateKey();
    void loginDownloadAndReuseSession();
    void reportsSmsAndLoginFailure();
    void rejectsPermissionRedirectAndNonAudio();
    void followsTrustedRedirect();
    void cancelsAndSkipsAmbiguousLyrics();
    void downloadsCoverAndPreservesExistingFile();
    void rejectsInvalidCoverWithoutLosingAudio();
};

void KugouMusicTest::rejectsInsecureWorkerUrl()
{
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    FakeManager manager;
    KugouMusicService remote(nullptr, &manager, QUrl(QStringLiteral("http://worker.example")),
                             dir.filePath(QStringLiteral("remote.json")), dir.filePath(QStringLiteral("Music")));
    QVERIFY(!remote.status().value(QStringLiteral("configured")).toBool());
    QVERIFY(!remote.startSearch(QStringLiteral("Song"), 1).isEmpty());
    QCOMPARE(manager.calls.size(), 0);
    KugouMusicService local(nullptr, &manager, QUrl(QStringLiteral("http://127.0.0.1:8787")),
                            dir.filePath(QStringLiteral("local.json")), dir.filePath(QStringLiteral("Music")));
    QVERIFY(local.status().value(QStringLiteral("configured")).toBool());
}

void KugouMusicTest::storesAndClearsPrivateKey()
{
    qputenv("KUGOU_ACCOUNT_API_KEY", "environment-key");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto keyPath = dir.filePath(QStringLiteral("config/kugou-account-key"));
    const auto sessionPath = dir.filePath(QStringLiteral("session.json"));
    const auto musicPath = dir.filePath(QStringLiteral("Music"));
    FakeManager manager;
    manager.routes.insert(QStringLiteral("/register/dev"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("cookies"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device")}}}})});
    manager.routes.insert(QStringLiteral("/captcha/sent"), {json({{QStringLiteral("status"), 1}})});
    {
        KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")),
                                  sessionPath, musicPath, keyPath);
        QVERIFY(service.status().value(QStringLiteral("configured")).toBool());
        QVERIFY(!service.status().value(QStringLiteral("key_saved")).toBool());
        QVERIFY(!service.saveAccountKey(QStringLiteral("\n")).isEmpty());
        QVERIFY(!service.saveAccountKey(QStringLiteral("part\nother")).isEmpty());
        QVERIFY(!service.saveAccountKey(QString(4097, QLatin1Char('x'))).isEmpty());
        QVERIFY(!service.saveAccountKey(QString(2049, QChar(0x4e2d))).isEmpty());
        QVERIFY(!QFileInfo::exists(keyPath));
        QCOMPARE(service.saveAccountKey(QStringLiteral("saved-key")), QString());
        QVERIFY(service.status().value(QStringLiteral("key_saved")).toBool());
        QCOMPARE(QFileInfo(keyPath).permissions()
                     & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther
                        | QFileDevice::WriteOther | QFileDevice::ExeGroup | QFileDevice::ExeOther),
                 QFileDevice::Permissions{});
        QFile stored(keyPath);
        QVERIFY(stored.open(QIODevice::ReadOnly));
        QCOMPARE(stored.readAll(), QByteArray("saved-key"));
        QVERIFY(!QJsonDocument(service.status()).toJson().contains("saved-key"));
    }
    {
        KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")),
                                  sessionPath, musicPath, keyPath);
        QVERIFY(service.status().value(QStringLiteral("key_saved")).toBool());
        QSignalSpy events(&service, &KugouMusicService::eventReady);
        QCOMPARE(service.startCodeRequest(QStringLiteral("13800138000")), QString());
        QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.code_sent")));
        QCOMPARE(manager.accountHeaders.value(QStringLiteral("/register/dev")), QByteArray("saved-key"));
        QCOMPARE(service.clearAccountKey(), QString());
        QVERIFY(!QFileInfo::exists(keyPath));
        QVERIFY(!service.status().value(QStringLiteral("key_saved")).toBool());
        QVERIFY(service.status().value(QStringLiteral("configured")).toBool());
        QCOMPARE(service.startCodeRequest(QStringLiteral("13800138000")), QString());
        QTRY_COMPARE(manager.calls.value(QStringLiteral("/captcha/sent")), 2);
        QCOMPARE(manager.accountHeaders.value(QStringLiteral("/captcha/sent")), QByteArray("environment-key"));
    }
}

void KugouMusicTest::loginDownloadAndReuseSession()
{
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    FakeManager manager;
    manager.routes.insert(QStringLiteral("/register/dev"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("cookies"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device")}}}})});
    manager.routes.insert(QStringLiteral("/captcha/sent"), {json({{QStringLiteral("status"), 1}})});
    manager.routes.insert(QStringLiteral("/login/cellphone"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("data"), QJsonObject{{QStringLiteral("token"), QStringLiteral("token")},
                                                                        {QStringLiteral("userid"), QStringLiteral("123")}}},
                                 {QStringLiteral("cookies"), QJsonObject{{QStringLiteral("token"), QStringLiteral("token")},
                                                                           {QStringLiteral("userid"), QStringLiteral("123")}}}})});
    configureSearch(manager);
    manager.routes.insert(QStringLiteral("/song/url"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(QStringLiteral("/song.mp3"), {"ID3test-audio", 200, QStringLiteral("audio/mpeg")});
    manager.routes.insert(QStringLiteral("/search/lyric"),
                          {json({{QStringLiteral("status"), 200},
                                 {QStringLiteral("candidates"), QJsonArray{QJsonObject{
                                     {QStringLiteral("song"), QStringLiteral("Song")},
                                     {QStringLiteral("singer"), QStringLiteral("Singer")},
                                     {QStringLiteral("duration"), 120000},
                                     {QStringLiteral("id"), 12},
                                     {QStringLiteral("accesskey"), QStringLiteral("abc")}}}}})});
    manager.routes.insert(QStringLiteral("/lyric"),
                          {json({{QStringLiteral("decodeContent"), QStringLiteral("[00:01.00]Hello\n")}})});
    const auto binaryKrc = QByteArray::fromBase64(
        "a3JjMTjb6lmXhn4OfBi4yEQcISJrCUzGbAZERyO2laxA9/4AI+cCEzSSFye2p6u+XX0OCkJTVkFeO6zTtiHUiRajK3JZRphRTodSJ3jhjTng0hPUV+Y=");
    manager.routes.insert(QStringLiteral("/download"),
                          {json({{QStringLiteral("status"), 200},
                                 {QStringLiteral("content"), QString::fromLatin1(binaryKrc.toBase64())}})});
    const auto session = dir.filePath(QStringLiteral("session.json"));
    const auto music = dir.filePath(QStringLiteral("Music"));
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")), session, music);
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QSignalSpy audio(&service, &KugouMusicService::audioReady);
    QCOMPARE(service.status().value(QStringLiteral("logged_in")).toBool(), false);
    QCOMPARE(service.startCodeRequest(QStringLiteral("13800138000")), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.code_sent")));
    QCOMPARE(manager.calls.value(QStringLiteral("/register/dev")), 1);
    QCOMPARE(manager.calls.value(QStringLiteral("/captcha/sent")), 1);
    QVERIFY(QFileInfo::exists(session));
    QCOMPARE(QFileInfo(session).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
             QFileDevice::Permissions{});

    events.clear();
    QCOMPARE(service.startLogin(QStringLiteral("13800138000"), QStringLiteral("123456")), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.logged_in")));
    QVERIFY(service.status().value(QStringLiteral("logged_in")).toBool());
    events.clear();
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 2), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.search_results")));
    QCOMPARE(events.last().at(0).value<QJsonObject>().value(QStringLiteral("page")).toInt(), 2);
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 1);
    const auto path = audio.at(0).at(0).toString();
    QCOMPARE(audio.at(0).at(1).toString(), QStringLiteral("saved"));
    QVERIFY(QFileInfo(path).isFile());
    const auto lrcPath = QFileInfo(path).absolutePath() + QLatin1Char('/')
                         + QFileInfo(path).completeBaseName() + QStringLiteral(".lrc");
    QVERIFY(QFileInfo(lrcPath).isFile());
    const auto krcPath = QFileInfo(path).absolutePath() + QLatin1Char('/')
                         + QFileInfo(path).completeBaseName() + QStringLiteral(".krc");
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
    KugouMusicService resumed(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")), session, music);
    QVERIFY(resumed.status().value(QStringLiteral("logged_in")).toBool());
}

void KugouMusicTest::rejectsPermissionRedirectAndNonAudio()
{
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    const auto session = dir.filePath(QStringLiteral("session.json"));
    QFile file(session);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(json({{QStringLiteral("version"), 1}, {QStringLiteral("cookies"), QJsonObject{
        {QStringLiteral("dfid"), QStringLiteral("device")}, {QStringLiteral("token"), QStringLiteral("token")},
        {QStringLiteral("userid"), QStringLiteral("123")}}}}));
    file.close();
    FakeManager manager;
    configureSearch(manager);
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")),
                              session, dir.filePath(QStringLiteral("Music")));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.search_results")));

    manager.routes.insert(QStringLiteral("/song/url"),
                          {json({{QStringLiteral("status"), 0}, {QStringLiteral("priv_status"), 0},
                                 {QStringLiteral("fail_process"), QJsonArray{QStringLiteral("pkg")}}})});
    events.clear();
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));

    manager.routes.insert(QStringLiteral("/song/url"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(QStringLiteral("/song.mp3"), {"", 302, QStringLiteral("audio/mpeg"), QUrl(QStringLiteral("https://evil.example/song.mp3"))});
    events.clear();
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));
    QCOMPARE(manager.calls.value(QStringLiteral("/song.mp3")), 1);

    manager.routes.insert(QStringLiteral("/song.mp3"), {"<html>bad</html>", 200, QStringLiteral("text/html")});
    events.clear();
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));

    manager.routes.insert(QStringLiteral("/song.mp3"), {"ID3", 200, QStringLiteral("audio/mpeg"), {}, false, 101LL * 1024 * 1024});
    events.clear();
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));
    QVERIFY(QDir(dir.filePath(QStringLiteral("Music"))).entryList(QDir::Files).isEmpty());
}

void KugouMusicTest::reportsSmsAndLoginFailure()
{
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    FakeManager manager;
    manager.routes.insert(QStringLiteral("/register/dev"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("cookies"), QJsonObject{{QStringLiteral("dfid"), QStringLiteral("device")}}}})});
    manager.routes.insert(QStringLiteral("/captcha/sent"),
                          {json({{QStringLiteral("status"), 0}, {QStringLiteral("error_code"), 400}})});
    manager.routes.insert(QStringLiteral("/login/cellphone"),
                          {json({{QStringLiteral("status"), 0}, {QStringLiteral("error_code"), 20028}})});
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")),
                              dir.filePath(QStringLiteral("session.json")), dir.filePath(QStringLiteral("Music")));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QCOMPARE(service.startCodeRequest(QStringLiteral("13800138000")), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));
    events.clear();
    QCOMPARE(service.startLogin(QStringLiteral("13800138000"), QStringLiteral("123456")), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.operation_failed")));
    QVERIFY(!service.status().value(QStringLiteral("logged_in")).toBool());
}

void KugouMusicTest::followsTrustedRedirect()
{
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    const auto session = dir.filePath(QStringLiteral("session.json"));
    QFile file(session);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(json({{QStringLiteral("version"), 1}, {QStringLiteral("cookies"), QJsonObject{
        {QStringLiteral("dfid"), QStringLiteral("device")}, {QStringLiteral("token"), QStringLiteral("token")},
        {QStringLiteral("userid"), QStringLiteral("123")}}}}));
    file.close();
    FakeManager manager;
    configureSearch(manager);
    manager.routes.insert(QStringLiteral("/song/url"),
                          {json({{QStringLiteral("status"), 200},
                                 {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(QStringLiteral("/song.mp3"),
                          {"", 302, QStringLiteral("audio/mpeg"), QUrl(QStringLiteral("https://cdn.kugou.com/final.mp3"))});
    manager.routes.insert(QStringLiteral("/final.mp3"), {"ID3test", 200, QStringLiteral("audio/mpeg")});
    manager.routes.insert(QStringLiteral("/search/lyric"),
                          {json({{QStringLiteral("status"), 404}, {QStringLiteral("candidates"), QJsonArray{}}})});
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")),
                              session, dir.filePath(QStringLiteral("Music")));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QSignalSpy audio(&service, &KugouMusicService::audioReady);
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.search_results")));
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 1);
    QCOMPARE(audio.at(0).at(1).toString(), QStringLiteral("none"));
    QCOMPARE(manager.calls.value(QStringLiteral("/final.mp3")), 1);
}

void KugouMusicTest::cancelsAndSkipsAmbiguousLyrics()
{
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    const auto session = dir.filePath(QStringLiteral("session.json"));
    QFile file(session);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(json({{QStringLiteral("version"), 1}, {QStringLiteral("cookies"), QJsonObject{
        {QStringLiteral("dfid"), QStringLiteral("device")}, {QStringLiteral("token"), QStringLiteral("token")},
        {QStringLiteral("userid"), QStringLiteral("123")}}}}));
    file.close();
    FakeManager manager;
    configureSearch(manager);
    manager.routes.insert(QStringLiteral("/song/url"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(QStringLiteral("/song.mp3"), {"ID3partial", 200, QStringLiteral("audio/mpeg"), {}, true});
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")),
                              session, dir.filePath(QStringLiteral("Music")));
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
                                {QStringLiteral("duration"), 120000}, {QStringLiteral("id"), 1},
                                {QStringLiteral("accesskey"), QStringLiteral("abc")}};
    manager.routes.insert(QStringLiteral("/search/lyric"),
                          {json({{QStringLiteral("status"), 200},
                                 {QStringLiteral("candidates"), QJsonArray{candidate, candidate}}})});
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 1);
    QCOMPARE(audio.at(0).at(1).toString(), QStringLiteral("uncertain"));
    QCOMPARE(manager.calls.value(QStringLiteral("/lyric")), 0);

    manager.routes.insert(QStringLiteral("/search/lyric"),
                          {json({{QStringLiteral("status"), 200},
                                 {QStringLiteral("candidates"), QJsonArray{candidate}}})});
    manager.routes.insert(QStringLiteral("/download"),
                          {R"({"status":200,"content":"pending"})", 200,
                           QStringLiteral("application/json"), {}, true});
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(manager.calls.value(QStringLiteral("/download")), 1);
    QCOMPARE(service.cancelDownload(), QString());
    QTRY_COMPARE(audio.count(), 2);
    QCOMPARE(audio.at(1).at(1).toString(), QStringLiteral("skipped"));
    QCOMPARE(manager.calls.value(QStringLiteral("/lyric")), 0);
    const QFileInfo downloaded(audio.at(1).at(0).toString());
    QVERIFY(!QFileInfo::exists(downloaded.absolutePath() + QLatin1Char('/')
                              + downloaded.completeBaseName() + QStringLiteral(".krc")));
}

void KugouMusicTest::downloadsCoverAndPreservesExistingFile()
{
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    const auto session = dir.filePath(QStringLiteral("session.json"));
    QFile file(session);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(json({{QStringLiteral("version"), 1}, {QStringLiteral("cookies"), QJsonObject{
        {QStringLiteral("dfid"), QStringLiteral("device")}, {QStringLiteral("token"), QStringLiteral("token")},
        {QStringLiteral("userid"), QStringLiteral("123")}}}}));
    file.close();
    FakeManager manager;
    configureSearchWithCover(manager, QStringLiteral("http://imge.kugou.com/stdmusic/{size}/cover.jpg"));
    manager.routes.insert(QStringLiteral("/song/url"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(QStringLiteral("/song.mp3"), {"ID3audio", 200, QStringLiteral("audio/mpeg")});
    manager.routes.insert(QStringLiteral("/search/lyric"),
                          {json({{QStringLiteral("status"), 404}, {QStringLiteral("candidates"), QJsonArray{}}})});
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "PNG"));
    manager.routes.insert(QStringLiteral("/stdmusic/240/cover.jpg"), {png, 200, QStringLiteral("image/png")});
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")),
                              session, dir.filePath(QStringLiteral("Music")));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QSignalSpy audio(&service, &KugouMusicService::audioReady);
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.search_results")));
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 1);
    QCOMPARE(audio.at(0).at(2).toString(), QStringLiteral("saved"));
    const auto downloaded = QFileInfo(audio.at(0).at(0).toString());
    const auto cover = downloaded.absolutePath() + QLatin1Char('/')
        + downloaded.completeBaseName() + QStringLiteral(".png");
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

void KugouMusicTest::rejectsInvalidCoverWithoutLosingAudio()
{
    qputenv("KUGOU_ACCOUNT_API_KEY", "test-key");
    QTemporaryDir dir;
    const auto session = dir.filePath(QStringLiteral("session.json"));
    QFile file(session);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(json({{QStringLiteral("version"), 1}, {QStringLiteral("cookies"), QJsonObject{
        {QStringLiteral("dfid"), QStringLiteral("device")}, {QStringLiteral("token"), QStringLiteral("token")},
        {QStringLiteral("userid"), QStringLiteral("123")}}}}));
    file.close();
    FakeManager manager;
    configureSearchWithCover(manager, QStringLiteral("https://imge.kugou.com/stdmusic/{size}/bad.jpg"));
    manager.routes.insert(QStringLiteral("/song/url"),
                          {json({{QStringLiteral("status"), 1},
                                 {QStringLiteral("url"), QJsonArray{QStringLiteral("https://audio.kugou.com/song.mp3")}}})});
    manager.routes.insert(QStringLiteral("/song.mp3"), {"ID3audio", 200, QStringLiteral("audio/mpeg")});
    manager.routes.insert(QStringLiteral("/search/lyric"),
                          {json({{QStringLiteral("status"), 404}, {QStringLiteral("candidates"), QJsonArray{}}})});
    manager.routes.insert(QStringLiteral("/stdmusic/240/bad.jpg"),
                          {"<html>not image</html>", 200, QStringLiteral("image/jpeg")});
    KugouMusicService service(nullptr, &manager, QUrl(QStringLiteral("https://worker.example")),
                              session, dir.filePath(QStringLiteral("Music")));
    QSignalSpy events(&service, &KugouMusicService::eventReady);
    QSignalSpy audio(&service, &KugouMusicService::audioReady);
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(hasEvent(events, QStringLiteral("kugou.search_results")));
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 1);
    QCOMPARE(audio.at(0).at(2).toString(), QStringLiteral("error"));
    const auto downloaded = QFileInfo(audio.at(0).at(0).toString());
    QVERIFY(downloaded.isFile());
    QVERIFY(!QFileInfo(downloaded.absolutePath() + QLatin1Char('/')
                      + downloaded.completeBaseName() + QStringLiteral(".jpg")).exists());
    configureSearchWithCover(manager, QStringLiteral("https://evil.example/cover.jpg"));
    QCOMPARE(service.startSearch(QStringLiteral("Song"), 1), QString());
    QTRY_VERIFY(manager.calls.value(QStringLiteral("/search")) == 2);
    QTRY_VERIFY(!service.status().value(QStringLiteral("busy")).toBool());
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
    QTRY_VERIFY(manager.calls.value(QStringLiteral("/search")) == 3);
    QTRY_VERIFY(!service.status().value(QStringLiteral("busy")).toBool());
    QCOMPARE(service.startDownload(QString::fromLatin1(kHash)), QString());
    QTRY_COMPARE(audio.count(), 3);
    QCOMPARE(audio.at(2).at(2).toString(), QStringLiteral("saved"));
    QCOMPARE(manager.calls.value(QStringLiteral("/song.mp3")), 1);
    QVERIFY(QFileInfo(downloaded.absolutePath() + QLatin1Char('/')
                      + downloaded.completeBaseName() + QStringLiteral(".png")).isFile());
}

QTEST_GUILESS_MAIN(KugouMusicTest)
#include "kugou_music_test.moc"
