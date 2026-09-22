#include "lyrics/lrc_parser.h"
#include "lyrics/lrclib_provider.h"
#include "lyrics/lyrics_service.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <QtTest/QtTest>
#include <cstring>

using namespace nekotune;

class MockProvider final : public LyricsProvider {
  public:
    int requests = 0;
    quint64 token = 0;
    bool search = false;
    void request(const LyricsQuery &, quint64 value, bool searching) override
    {
        ++requests;
        token = value;
        search = searching;
    }
    void cancel() override {}
};

class MockReply final : public QNetworkReply {
  public:
    MockReply(const QNetworkRequest &request, const QByteArray &body, int status, NetworkError error, bool hang,
              QObject *parent)
        : QNetworkReply(parent), m_body(body)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly);
        if (!hang)
            QTimer::singleShot(0, this, [this, error]() {
                if (isFinished())
                    return;
                if (error != NoError)
                    setError(error, QStringLiteral("Mock network failure"));
                emit readyRead();
                setFinished(true);
                emit finished();
            });
    }
    void abort() override
    {
        if (isFinished())
            return;
        setError(OperationCanceledError, QStringLiteral("Canceled"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

  protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const auto size = qMin(maxSize, static_cast<qint64>(m_body.size() - m_offset));
        if (size <= 0)
            return -1;
        std::memcpy(data, m_body.constData() + m_offset, size);
        m_offset += size;
        return size;
    }

  private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class MockManager final : public QNetworkAccessManager {
  public:
    QByteArray body;
    int status = 200;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    bool hang = false;
    QNetworkRequest lastRequest;

  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        lastRequest = request;
        return new MockReply(request, body, status, error, hang, this);
    }
};

static LyricsQuery query()
{
    return {QStringLiteral("Song"), QStringLiteral("Artist"), QStringLiteral("Album"), 180000,
            QStringLiteral("audio-hash")};
}

static LyricsCandidate candidate()
{
    LyricsDocument document;
    document.source = QStringLiteral("lrclib");
    document.matched = query();
    document.providerId = 42;
    document.syncedLyrics = QStringLiteral("[00:01.00]First\n[00:02.345]Second");
    return {document};
}

static QByteArray response()
{
    return R"({"id":42,"trackName":"Song","artistName":"Artist","albumName":"Album","duration":180,"instrumental":false,"syncedLyrics":"[00:01.00]Hello","plainLyrics":"Hello"})";
}

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

class LyricsTest final : public QObject {
    Q_OBJECT
  private slots:
    void timestampsAndMultipleTags();
    void invalidAndEmptyLines();
    void stableSortAndOffset();
    void plainFallbackAndInstrumental();
    void matchingAndDuration();
    void cacheIdentity();
    void cacheRoundTripAndAtomicWrite();
    void localAndCachePriority();
    void staleRepliesAndSelection();
    void autoMatchAndManualSearch();
    void cacheFailureStillDisplays();
    void offlineCancelsRequests();
    void networkFailures_data();
    void networkFailures();
    void providerPayloadsAndRequest();
    void providerCancellationAndDestruction();
};

void LyricsTest::timestampsAndMultipleTags()
{
    const auto lines = LrcParser::parse(QStringLiteral("[ar:Artist]\r\n[01:02.34][02:03.456]Words\r\n[00:03.1]Start"));
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0).timestampMs, 3100);
    QCOMPARE(lines.at(1).timestampMs, 62340);
    QCOMPARE(lines.at(2).timestampMs, 123456);
    QCOMPARE(lines.at(1).text, QStringLiteral("Words"));
    QCOMPARE(lines.at(2).text, QStringLiteral("Words"));
}

void LyricsTest::invalidAndEmptyLines()
{
    const auto lines = LrcParser::parse(QStringLiteral("[00:01]hello\n[00:02.00]\n[00:99.99]bad\n[01:02.1234]"
                                                       "bad\n[00:03\n[ti:title]\nraw"));
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.last().timestampMs, 1000);
    QCOMPARE(lines.last().text, QStringLiteral("hello"));
    QVERIFY(!LrcParser::looksLikeLrc(QStringLiteral("[00:01]\n[ti:title]")));
    QVERIFY(LrcParser::parse(QStringLiteral("garbage\n[xx:yy]\n[12:")).isEmpty());
}

void LyricsTest::stableSortAndOffset()
{
    const auto lines = LrcParser::parse(QStringLiteral("[00:02]later\n[00:01]a\n[00:01]b\n[offset:-500]"));
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0).timestampMs, 500);
    QCOMPARE(lines.at(0).text, QStringLiteral("a"));
    QCOMPARE(lines.at(1).text, QStringLiteral("b"));
    QCOMPARE(lines.at(2).timestampMs, 1500);
}

void LyricsTest::plainFallbackAndInstrumental()
{
    auto document = candidate().document;
    document.syncedLyrics = QStringLiteral("[invalid]");
    document.plainLyrics = QStringLiteral("Plain lyrics\nNext line");
    document.validate();
    QVERIFY(!document.isSynced());
    QVERIFY(!document.isEmpty());
    QCOMPARE(document.toJson().value(QStringLiteral("plain_text")).toString(), document.plainLyrics);
    document.instrumental = true;
    document.validate();
    QVERIFY(document.syncedLyrics.isEmpty());
    QVERIFY(document.plainLyrics.isEmpty());
    QVERIFY(!document.isEmpty());
}

void LyricsTest::matchingAndDuration()
{
    auto q = query();
    q.title = QStringLiteral("  SONG  ");
    q.artist = QStringLiteral("ARTIST");
    auto exact = candidate();
    auto live = exact;
    live.document.matched.title = QStringLiteral("Song (Live)");
    auto wrongArtist = exact;
    wrongArtist.document.matched.artist = QStringLiteral("Other");
    auto longVersion = exact;
    longVersion.document.matched.durationMs += 12000;
    const auto ranked = LyricsService::rankCandidates(q, {live, wrongArtist, longVersion, exact});
    QCOMPARE(ranked.first().document.matched.title, QStringLiteral("Song"));
    QCOMPARE(ranked.first().document.matched.durationMs, 180000);
    QVERIFY(LyricsService::confident(q, ranked));
    QVERIFY(!LyricsService::confident(q, {wrongArtist}));
    QVERIFY(!LyricsService::confident(q, {live}));
    QVERIFY(!LyricsService::confident(q, {longVersion}));
    QVERIFY(!LyricsService::confident(q, LyricsService::rankCandidates(q, {exact, exact})));
    exact.document.matched.durationMs += 2000;
    QVERIFY(LyricsService::confident(q, {exact}));
    ++exact.document.matched.durationMs;
    QVERIFY(!LyricsService::confident(q, {exact}));
    q.durationMs = 0;
    QVERIFY(!LyricsService::confident(q, {exact}));
    q = query();
    q.album = QStringLiteral("Other release");
    QVERIFY(!LyricsService::confident(q, {candidate()}));
}

void LyricsTest::cacheIdentity()
{
    QTemporaryDir temp;
    LyricsCache cache(temp.path());
    auto a = query();
    auto b = a;
    b.title = QStringLiteral("Another title");
    QCOMPARE(cache.keyFor(a),
             cache.keyFor(b)); // Audio content identifies copies and corrected metadata.
    b.trackId = QStringLiteral("different-recording");
    QVERIFY(cache.keyFor(a) != cache.keyFor(b));
    a.trackId.clear();
    b = a;
    b.artist = QStringLiteral("  ARTIST  ");
    QCOMPARE(cache.keyFor(a), cache.keyFor(b));
    b.artist = QStringLiteral("Other Artist");
    QVERIFY(cache.keyFor(a) != cache.keyFor(b));
    b = a;
    b.durationMs += 1000;
    QVERIFY(cache.keyFor(a) != cache.keyFor(b));
    QVERIFY(QRegularExpression(QStringLiteral("^[a-f0-9]{64}$")).match(cache.keyFor(a)).hasMatch());
}

void LyricsTest::cacheRoundTripAndAtomicWrite()
{
    QTemporaryDir temp;
    LyricsCache cache(temp.path());
    auto document = candidate().document;
    QVERIFY(cache.write(query(), document));
    auto cached = cache.read(query());
    QVERIFY(cached.has_value());
    QCOMPARE(cached->syncedLyrics, document.syncedLyrics);
    QCOMPARE(cached->matched.album, document.matched.album);
    QCOMPARE(cached->providerId, document.providerId);
    const auto path = QDir(temp.path()).filePath(cache.keyFor(query()) + QStringLiteral(".json"));
    // A reader sees the complete prior file while an atomic replacement is
    // unfinished.
    QSaveFile unfinished(path);
    QVERIFY(unfinished.open(QIODevice::WriteOnly));
    unfinished.write("partial");
    QCOMPARE(cache.read(query())->syncedLyrics, document.syncedLyrics);
    unfinished.cancelWriting();
    document.syncedLyrics.clear();
    document.plainLyrics = QStringLiteral("Plain");
    QVERIFY(cache.write(query(), document));
    QCOMPARE(cache.read(query())->plainLyrics, document.plainLyrics);
    QCOMPARE(QDir(temp.path()).entryList(QDir::Files).size(), 1);
    writeFile(path, "{broken");
    QVERIFY(!cache.read(query()));
    document.instrumental = true;
    document.validate();
    QVERIFY(cache.write(query(), document));
    QVERIFY(cache.read(query())->instrumental);
}

void LyricsTest::localAndCachePriority()
{
    QTemporaryDir temp;
    MockProvider provider;
    LyricsService service(&provider, temp.filePath(QStringLiteral("cache")));
    QJsonObject state;
    connect(&service, &LyricsService::changed, this, [&](const auto &value) { state = value; });
    const auto path = temp.filePath(QStringLiteral("song.flac"));
    const auto lrc = temp.filePath(QStringLiteral("song.lrc"));
    writeFile(lrc, "[00:01]Sidecar");
    service.load(query(), path, {}, 1, false);
    QCOMPARE(provider.requests, 0);
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("source")).toString(),
             QStringLiteral("local"));
    service.load(query(), path, {}, 2, true, true);
    QCOMPARE(provider.requests, 0); // Refresh still respects local files.
    writeFile(lrc, "[broken]");
    service.load(query(), path, {}, 3, true);
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("plain_text")).toString(), QStringLiteral("[broken]"));
    QCOMPARE(provider.requests, 0);
    QVERIFY(QFile::remove(lrc));
    LyricsCache cache(temp.filePath(QStringLiteral("cache")));
    QVERIFY(cache.write(query(), candidate().document));
    service.setOffline(true);
    service.load(query(), path, {}, 4, false);
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    QVERIFY(state.value(QStringLiteral("cached")).toBool());
    QCOMPARE(provider.requests, 0);
    service.setOffline(false);
    service.load(query(), path, QStringLiteral("Custom text"), 5, true, true);
    QVERIFY(provider.requests > 0); // Forced refresh bypasses the persisted text.
    emit provider.completed(provider.token, {candidate()});
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("source")).toString(),
             QStringLiteral("lrclib"));
}

void LyricsTest::staleRepliesAndSelection()
{
    QTemporaryDir temp;
    MockProvider provider;
    LyricsService service(&provider, temp.path());
    QJsonObject state;
    connect(&service, &LyricsService::changed, this, [&](const auto &value) { state = value; });
    const auto a = query();
    auto b = a;
    b.trackId = QStringLiteral("other-recording-same-metadata");
    service.load(a, temp.filePath(QStringLiteral("a.mp3")), {}, 1, true);
    const auto firstToken = provider.token;
    service.load(b, temp.filePath(QStringLiteral("b.mp3")), {}, 2, true);
    const auto secondToken = provider.token;
    emit provider.completed(firstToken, {candidate()});
    emit provider.failed(firstToken, QStringLiteral("network"));
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("loading"));
    QCOMPARE(state.value(QStringLiteral("track_id")).toString(), b.trackId);
    emit provider.completed(secondToken, {candidate()});
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    service.search(a, 3);
    const auto searchToken = provider.token;
    emit provider.completed(searchToken, {candidate()});
    service.search(a,
                   4); // A -> B -> A, and an old selection from the same track.
    emit provider.completed(searchToken, {candidate()});
    service.select(0, 3);
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("searching"));
    service.clear(5);
    emit provider.completed(provider.token, {candidate()});
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("idle"));
}

void LyricsTest::autoMatchAndManualSearch()
{
    QTemporaryDir temp;
    MockProvider provider;
    LyricsService service(&provider, temp.path());
    QJsonObject state;
    connect(&service, &LyricsService::changed, this, [&](const auto &value) { state = value; });
    service.load(query(), {}, {}, 1, true);
    auto weak = candidate();
    weak.document.matched.artist = QStringLiteral("Other Artist");
    emit provider.completed(provider.token, {weak});
    QVERIFY(provider.search);
    emit provider.completed(provider.token, {weak});
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("candidates"));
    service.select(0, 1);
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    service.search(query(), 2);
    emit provider.completed(provider.token, {candidate()});
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("candidates"));
    service.select(0, 2);
    QCOMPARE(LyricsCache(temp.path()).read(query())->matched.artist, QStringLiteral("Artist"));
    service.load(query(), {}, {}, 3, true, true);
    QVERIFY(!provider.search);
    emit provider.failed(provider.token, QStringLiteral("not_found"));
    QVERIFY(provider.search);
    emit provider.completed(provider.token, {});
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("not_found"));
}

void LyricsTest::cacheFailureStillDisplays()
{
    QTemporaryDir temp;
    const auto path = temp.filePath(QStringLiteral("not-a-directory"));
    writeFile(path, "file");
    MockProvider provider;
    LyricsService service(&provider, path);
    QJsonObject state;
    connect(&service, &LyricsService::changed, this, [&](const auto &value) { state = value; });
    service.load(query(), {}, {}, 1, true);
    emit provider.completed(provider.token, {candidate()});
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    QVERIFY(state.value(QStringLiteral("cache_warning")).toBool());
    QVERIFY(!state.value(QStringLiteral("document")).toObject().isEmpty());
}

void LyricsTest::offlineCancelsRequests()
{
    QTemporaryDir temp;
    MockProvider provider;
    LyricsService service(&provider, temp.path());
    QJsonObject state;
    connect(&service, &LyricsService::changed, this, [&](const auto &value) { state = value; });
    service.load(query(), {}, {}, 1, true);
    const auto token = provider.token;
    service.setOffline(true);
    emit provider.completed(token, {candidate()});
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
    service.search(query(), 2);
    QCOMPARE(provider.requests, 1);
}

void LyricsTest::networkFailures_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<int>("status");
    QTest::addColumn<int>("networkError");
    QTest::addColumn<bool>("hang");
    QTest::addColumn<QString>("expected");
    QTest::newRow("timeout") << QByteArray() << 200 << 0 << true << QStringLiteral("timeout");
    QTest::newRow("empty-body") << QByteArray() << 200 << 0 << false << QStringLiteral("invalid_response");
    QTest::newRow("invalid-json") << QByteArray("{bad") << 200 << 0 << false << QStringLiteral("invalid_response");
    QTest::newRow("missing-fields") << QByteArray("[{}]") << 200 << 0 << false << QStringLiteral("invalid_response");
    QTest::newRow("wrong-shape") << response() << 200 << 0 << false << QStringLiteral("invalid_response");
    QTest::newRow("dns") << QByteArray() << 0 << int(QNetworkReply::HostNotFoundError) << false
                         << QStringLiteral("network");
    QTest::newRow("connection") << QByteArray() << 0 << int(QNetworkReply::ConnectionRefusedError) << false
                                << QStringLiteral("network");
    QTest::newRow("http500") << QByteArray() << 500 << 0 << false << QStringLiteral("network");
    QTest::newRow("http429") << QByteArray() << 429 << 0 << false << QStringLiteral("rate_limited");
    QTest::newRow("http404") << QByteArray() << 404 << 0 << false << QStringLiteral("not_found");
}

void LyricsTest::networkFailures()
{
    QFETCH(QByteArray, body);
    QFETCH(int, status);
    QFETCH(int, networkError);
    QFETCH(bool, hang);
    QFETCH(QString, expected);
    MockManager manager;
    manager.body = body;
    manager.status = status;
    manager.error = static_cast<QNetworkReply::NetworkError>(networkError);
    manager.hang = hang;
    LrclibProvider provider(nullptr, &manager, 20);
    QSignalSpy failed(&provider, &LyricsProvider::failed);
    provider.request(query(), 123, true);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(failed.first().at(0).toULongLong(), quint64(123));
    QCOMPARE(failed.first().at(1).toString(), expected);
}

void LyricsTest::providerPayloadsAndRequest()
{
    MockManager manager;
    manager.body = response();
    LrclibProvider provider(nullptr, &manager);
    QVector<LyricsCandidate> result;
    int completed = 0;
    connect(&provider, &LyricsProvider::completed, this, [&](quint64, const auto &items) {
        result = items;
        ++completed;
    });
    provider.request(query(), 1, false);
    QTRY_COMPARE(completed, 1);
    QCOMPARE(result.size(), 1);
    QVERIFY(result.first().document.isSynced());
    QVERIFY(manager.lastRequest.rawHeader("User-Agent").startsWith("NekoTune/"));
    const QUrlQuery params(manager.lastRequest.url());
    QCOMPARE(params.queryItemValue(QStringLiteral("duration")), QStringLiteral("180.000"));
    QCOMPARE(params.queryItemValue(QStringLiteral("album_name")), QStringLiteral("Album"));
    manager.body = "[]";
    provider.request(query(), 2, true);
    QTRY_COMPARE(completed, 2);
    QVERIFY(result.isEmpty());
    auto object = QJsonDocument::fromJson(response()).object();
    object.insert(QStringLiteral("syncedLyrics"), QStringLiteral("[bad]"));
    manager.body = QJsonDocument(object).toJson();
    provider.request(query(), 3, false);
    QTRY_COMPARE(completed, 3);
    QVERIFY(!result.first().document.isSynced());
    QCOMPARE(result.first().document.plainLyrics, QStringLiteral("Hello"));
    object.insert(QStringLiteral("instrumental"), true);
    object.insert(QStringLiteral("plainLyrics"), QJsonValue::Null);
    manager.body = QJsonDocument(object).toJson();
    provider.request(query(), 4, false);
    QTRY_COMPARE(completed, 4);
    QVERIFY(result.first().document.instrumental);
    object.insert(QStringLiteral("syncedLyrics"), 123);
    manager.body = QJsonDocument(object).toJson();
    QSignalSpy failed(&provider, &LyricsProvider::failed);
    provider.request(query(), 5, false);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(failed.first().at(1).toString(), QStringLiteral("invalid_response"));
}

void LyricsTest::providerCancellationAndDestruction()
{
    MockManager manager;
    manager.hang = true;
    auto *provider = new LrclibProvider(nullptr, &manager, 20);
    int callbacks = 0;
    connect(provider, &LyricsProvider::completed, this, [&]() { ++callbacks; });
    connect(provider, &LyricsProvider::failed, this, [&]() { ++callbacks; });
    provider->request(query(), 1, false);
    provider->cancel();
    QTest::qWait(40);
    QCOMPARE(callbacks, 0);
    provider->request(query(), 2, false);
    delete provider;
    QTest::qWait(40);
    QCOMPARE(callbacks, 0);
}

QTEST_GUILESS_MAIN(LyricsTest)
#include "lyrics_test.moc"
