#include "lyrics/lrc_parser.h"
#include "lyrics/krc_parser.h"
#include "lyrics/lrclib_provider.h"
#include "lyrics/kugou_provider.h"
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
    QHash<QString, QByteArray> bodies;
    int status = 200;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    bool hang = false;
    QNetworkRequest lastRequest;

  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        lastRequest = request;
        return new MockReply(request, bodies.value(request.url().path(), body), status, error, hang, this);
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
    void ignoresLegacyAsrLyrics();
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
    void kugouSearchSelectAndCache();
    void kugouGroupCovers();
    void krcDecodeAndPriority();
    void kugouKrcSelectionAndCancellation();
};

static QByteArray sampleKrc()
{
    return QByteArray::fromBase64("a3JjMTjb6lmXhn4OfBi4yEQcISJrCUzGbAZERyO2laxA9/4AI+cCEzSSFye2p6u+XX0OCkJTVkFeO6zTtiHUiRajK3JZRphRTodSJ3jhjTng0hPUV+Y=");
}

void LyricsTest::krcDecodeAndPriority()
{
    const auto decoded = KrcParser::decode(sampleKrc());
    QVERIFY(decoded.has_value());
    const auto lines = KrcParser::parse(*decoded);
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0).timestampMs, 1100);
    QCOMPARE(lines.at(0).words.size(), 2);
    QCOMPARE(lines.at(0).words.at(0).text, QStringLiteral("Hello "));
    QCOMPARE(lines.at(0).words.at(1).timestampMs, 1500);
    QCOMPARE(lines.at(0).words.at(1).durationMs, 300);
    QCOMPARE(lines.at(0).words.at(1).text, QStringLiteral("<world>"));
    QVERIFY(!KrcParser::decode(sampleKrc().left(12)));
    auto damaged = sampleKrc();
    damaged[4] = char(0);
    QVERIFY(!KrcParser::decode(damaged));

    QTemporaryDir temp;
    const auto audio = temp.filePath(QStringLiteral("song.flac"));
    const auto krc = temp.filePath(QStringLiteral("song.krc"));
    const auto lrc = temp.filePath(QStringLiteral("song.lrc"));
    writeFile(krc, sampleKrc());
    writeFile(lrc, "[00:01]LRC");
    MockProvider provider;
    LyricsService service(&provider, temp.filePath(QStringLiteral("cache")));
    QJsonObject state;
    connect(&service, &LyricsService::changed, this, [&](const auto &value) { state = value; });
    service.setOffline(true);
    service.load(query(), audio, QStringLiteral("Custom"), 1, true);
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("format")).toString(),
             QStringLiteral("krc"));
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("lines")).toArray()
                 .first().toObject().value(QStringLiteral("words")).toArray().size(), 2);
    QCOMPARE(provider.requests, 0);
    QVERIFY(QFile::remove(krc));
    writeFile(krc, "invalid krc");
    service.load(query(), audio, QStringLiteral("Custom"), 2, true);
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("format")).toString(),
             QStringLiteral("lrc"));
    QVERIFY(QFile::remove(lrc));
    QVERIFY(QFile::remove(krc));
    LyricsDocument cached;
    cached.source = QStringLiteral("kugou");
    cached.krcLyrics = *decoded;
    QVERIFY(LyricsCache(temp.filePath(QStringLiteral("cache"))).write(query(), cached));
    service.load(query(), audio, QStringLiteral("Custom"), 3, true);
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("source")).toString(),
             QStringLiteral("custom"));
    service.load(query(), audio, {}, 4, true);
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("format")).toString(),
             QStringLiteral("krc"));
    writeFile(krc, decoded->toUtf8());
    service.load(query(), audio, {}, 5, true);
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("format")).toString(),
             QStringLiteral("krc"));
}

void LyricsTest::kugouKrcSelectionAndCancellation()
{
    MockManager manager;
    manager.bodies.insert(QStringLiteral("/search"), R"({"status":1,"error_code":0,"data":{"lists":[
        {"SongName":"Song","SingerName":"Artist","FileHash":"0123456789abcdef0123456789abcdef","Duration":180}]}})");
    manager.bodies.insert(QStringLiteral("/search/lyric"), R"({"status":200,"candidates":[
        {"id":456,"accesskey":"secretkey","song":"Song","singer":"Artist","duration":180000}]})");
    manager.bodies.insert(QStringLiteral("/download"), QByteArray(R"({"status":200,"content":")")
                          + sampleKrc().toBase64() + R"("})");
    KugouProvider kugou(nullptr, &manager, QUrl(QStringLiteral("https://example.invalid")));
    MockProvider lrclib;
    QTemporaryDir temp;
    LyricsService service(&lrclib, temp.path(), nullptr, &kugou);
    QJsonObject state;
    connect(&service, &LyricsService::changed, this, [&](const auto &value) { state = value; });
    service.search(query(), 1, QStringLiteral("kugou"));
    QTRY_COMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("candidates"));
    service.select(0, 1);
    QTRY_COMPARE(state.value(QStringLiteral("search_stage")).toString(), QStringLiteral("lyrics"));
    QTRY_COMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("candidates"));
    service.select(0, 1);
    QTRY_COMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("format")).toString(),
             QStringLiteral("krc"));
    QCOMPARE(QUrlQuery(manager.lastRequest.url()).queryItemValue(QStringLiteral("fmt")), QStringLiteral("krc"));
    QVERIFY(!QJsonDocument(state).toJson().contains("secretkey"));
    QVERIFY(LyricsCache(temp.path()).read(query())->isSynced());

    service.search(query(), 2, QStringLiteral("kugou"));
    QTRY_COMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("candidates"));
    service.select(0, 2);
    QTRY_COMPARE(state.value(QStringLiteral("search_stage")).toString(), QStringLiteral("lyrics"));
    QTRY_COMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("candidates"));
    manager.hang = true;
    service.select(0, 2);
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("loading"));
    service.clear(3);
    QTest::qWait(20);
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("idle"));
}

void LyricsTest::ignoresLegacyAsrLyrics()
{
    QTemporaryDir temp;
    const auto audio = temp.filePath(QStringLiteral("song.wav"));
    writeFile(temp.filePath(QStringLiteral("song.asr.json")), R"({"transcripts":[]})");
    const auto cachePath = temp.filePath(QStringLiteral("cache"));
    LyricsDocument legacy;
    legacy.source = QStringLiteral("aliyun_asr");
    legacy.syncedLyrics = QStringLiteral("[00:01.00]Old ASR text");
    QVERIFY(LyricsCache(cachePath).write(query(), legacy));
    QVERIFY(!LyricsCache(cachePath).read(query()));
    MockProvider provider;
    LyricsService service(&provider, cachePath);
    QJsonObject state;
    connect(&service, &LyricsService::changed, this, [&](const auto &value) { state = value; });
    service.setOffline(true);
    service.load(query(), audio, {}, 1, true);
    QCOMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
    QVERIFY(!state.contains(QStringLiteral("document")));
    QCOMPARE(provider.requests, 0);
    writeFile(temp.filePath(QStringLiteral("song.lrc")), "[00:02.00]Current LRC");
    service.load(query(), audio, {}, 2, true);
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("source")).toString(),
             QStringLiteral("local"));
}

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
    const QJsonObject oldCache{{QStringLiteral("version"), 1},
                               {QStringLiteral("key"), cache.keyFor(query())},
                               {QStringLiteral("source"), QStringLiteral("lrclib")},
                               {QStringLiteral("synced_lyrics"), QStringLiteral("[00:01]Legacy")},
                               {QStringLiteral("plain_lyrics"), QString()},
                               {QStringLiteral("instrumental"), false}};
    writeFile(path, QJsonDocument(oldCache).toJson());
    QCOMPARE(cache.read(query())->syncedLyrics, QStringLiteral("[00:01]Legacy"));
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

void LyricsTest::kugouSearchSelectAndCache()
{
    MockManager manager;
    manager.bodies.insert(QStringLiteral("/search"), R"({"status":1,"error_code":0,"data":{"lists":[
        {"SongName":"<em>Song</em>","SingerName":"Artist","AlbumName":"Album",
         "FileHash":"0123456789abcdef0123456789abcdef","Duration":180,"MixSongID":123,
         "Image":"http://imge.kugou.com/stdmusic/{size}/cover.jpg"}]}})");
    manager.bodies.insert(QStringLiteral("/search/lyric"), R"({"status":200,"candidates":[
        {"id":456,"accesskey":"secretkey","song":"Song","singer":"Artist","duration":180000}]})");
    manager.bodies.insert(QStringLiteral("/lyric"), R"({"status":200,"decodeContent":"[00:01.00]Kugou line"})");
    KugouProvider kugou(nullptr, &manager, QUrl(QStringLiteral("https://example.invalid")));
    MockProvider lrclib;
    QTemporaryDir temp;
    LyricsService service(&lrclib, temp.path(), nullptr, &kugou);
    QJsonObject state;
    connect(&service, &LyricsService::changed, this, [&](const auto &value) { state = value; });
    service.search(query(), 1, QStringLiteral("kugou"));
    QTRY_COMPARE(state.value(QStringLiteral("search_stage")).toString(), QStringLiteral("songs"));
    QCOMPARE(state.value(QStringLiteral("candidates")).toArray().size(), 1);
    QVERIFY(state.value(QStringLiteral("candidates")).toArray().first().toObject().value(QStringLiteral("song_result")).toBool());
    const auto expectedCover = QStringLiteral("https://imge.kugou.com/stdmusic/240/cover.jpg");
    QCOMPARE(state.value(QStringLiteral("candidates")).toArray().first().toObject()
                 .value(QStringLiteral("cover_url")).toString(), expectedCover);
    QCOMPARE(lrclib.requests, 0);
    service.select(0, 1);
    QTRY_COMPARE(state.value(QStringLiteral("search_stage")).toString(), QStringLiteral("lyrics"));
    QTRY_COMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("candidates"));
    QCOMPARE(state.value(QStringLiteral("candidates")).toArray().first().toObject()
                 .value(QStringLiteral("cover_url")).toString(), expectedCover);
    QCOMPARE(QUrlQuery(manager.lastRequest.url()).queryItemValue(QStringLiteral("hash")),
             QStringLiteral("0123456789abcdef0123456789abcdef"));
    QVERIFY(!QJsonDocument(state).toJson().contains("secretkey"));
    service.select(0, 1);
    QTRY_COMPARE(state.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("source")).toString(),
             QStringLiteral("kugou"));
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("cover_url")).toString(),
             expectedCover);
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("lines")).toArray().size(), 1);
    QCOMPARE(LyricsCache(temp.path()).read(query())->source, QStringLiteral("kugou"));
    QCOMPARE(LyricsCache(temp.path()).read(query())->coverUrl, expectedCover);
    QVERIFY(!QJsonDocument(state).toJson().contains("secretkey"));
    QCOMPARE(QUrlQuery(manager.lastRequest.url()).queryItemValue(QStringLiteral("accesskey")), QStringLiteral("secretkey"));
    QVERIFY(manager.lastRequest.rawHeader("Authorization").isEmpty());
    QVERIFY(manager.lastRequest.rawHeader("Cookie").isEmpty());
    service.search(query(), 2, QStringLiteral("lrclib"));
    QCOMPARE(lrclib.requests, 1);
    service.load(query(), {}, {}, 3, true);
    QCOMPARE(state.value(QStringLiteral("document")).toObject().value(QStringLiteral("cover_url")).toString(),
             expectedCover);
    QCOMPARE(lrclib.requests, 1); // Cached lyrics and cover do not repeat the search.
}

void LyricsTest::kugouGroupCovers()
{
    MockManager manager;
    manager.body = R"({"status":1,"error_code":0,"data":{"lists":[
        {"SongName":"Song","FileHash":"0123456789abcdef0123456789abcdef",
         "Image":"http://imge.kugou.com/stdmusic/{size}/main.jpg","Grp":[
           {"FileHash":"abcdef0123456789abcdef0123456789","Image":"http://imge.kugou.com/stdmusic/{size}/group.jpg"},
           {"FileHash":"fedcba9876543210fedcba9876543210","Image":"https://invalid.example/cover.jpg"}]}
    ]}})";
    KugouProvider provider(nullptr, &manager, QUrl(QStringLiteral("https://example.invalid")));
    QVector<LyricsCandidate> results;
    connect(&provider, &LyricsProvider::completed, this,
            [&](quint64, const auto &candidates) { results = candidates; });
    provider.request(query(), 1, true);
    QTRY_COMPARE(results.size(), 3);
    QCOMPARE(results.at(0).document.coverUrl, QStringLiteral("https://imge.kugou.com/stdmusic/240/main.jpg"));
    QCOMPARE(results.at(1).document.coverUrl, QStringLiteral("https://imge.kugou.com/stdmusic/240/group.jpg"));
    QVERIFY(results.at(2).document.coverUrl.isEmpty());
}

QTEST_GUILESS_MAIN(LyricsTest)
#include "lyrics_test.moc"
