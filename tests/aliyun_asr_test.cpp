#include "lyrics/aliyun_asr.h"
#include "lyrics/network_diagnostics.h"
#include "storage/asr_settings.h"
#include "storage/song_store.h"
#include <QSqlQuery>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
using namespace nekotune;

class Reply final : public QNetworkReply {
  public:
    Reply(const QNetworkRequest &request, QByteArray body, int status, bool hang, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly);
        if (!hang)
            QTimer::singleShot(0, this, [this]() {
                if (isFinished())
                    return;
                emit readyRead();
                setFinished(true);
                emit finished();
            });
    }
    void networkError(NetworkError error)
    {
        setError(error, "https://private.example/result?signature=secret-url");
    }
    void abort() override
    {
        if (isFinished())
            return;
        setFinished(true);
        setError(OperationCanceledError, "Canceled");
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

  protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto count = qMin(maximum, qint64(m_body.size() - m_offset));
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
class Manager final : public QNetworkAccessManager {
  public:
    QList<QNetworkRequest> requests;
    QList<QByteArray> bodies;
    QByteArray transcription =
        R"({"file_url":"original-url","properties":{"original_duration_in_milliseconds":1000},"transcripts":[{"sentences":[{"begin_time":0,"end_time":1000,"text":"hello"}]}]})";
    int failAt = -1;
    int failStatus = 401;
    bool hang = false;
    bool subtaskFails = false;
    bool qwen3Result = false;
    bool missingResult = false;
    bool invalidTaskId = false;
    bool invalidPolicy = false;
    QByteArray failureBody;
    QNetworkReply::NetworkError failureNetwork = QNetworkReply::NoError;

  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *outgoing) override
    {
        const int step = requests.size();
        requests.append(request);
        bodies.append(outgoing ? outgoing->readAll() : QByteArray{});
        QByteArray body;
        if (step == 0)
            body =
                invalidPolicy
                    ? "{}"
                    : R"({"data":{"upload_host":"https://bucket.oss-cn-beijing.aliyuncs.com","upload_dir":"tmp/test","oss_access_key_id":"upload-key","signature":"signature","policy":"policy","x_oss_object_acl":"private","x_oss_forbid_overwrite":"true"}})";
        else if (step == 1)
            body = "";
        else if (step == 2)
            body = R"({"output":{"task_id":"task-123","task_status":"PENDING"}})";
        else if (step == 3)
            body = R"({"output":{"task_status":"RUNNING"}})";
        else if (step == 4)
            body =
                subtaskFails
                    ? R"({"output":{"task_status":"SUCCEEDED","results":[{"subtask_status":"FAILED"}]}})"
                    : R"({"output":{"task_status":"SUCCEEDED","results":[{"subtask_status":"SUCCEEDED","transcription_url":"https://result.oss-cn-beijing.aliyuncs.com/result.json?signature=signed"}]}})";
        else
            body = transcription;
        if (step == 2 && invalidTaskId)
            body = R"({"output":{"task_id":"invalid/id","task_status":"PENDING"}})";
        if (step == 4 && qwen3Result)
            body = missingResult ? QByteArray(R"({"output":{"task_status":"SUCCEEDED"}})")
                                 : QByteArray(R"({"output":{"task_status":"SUCCEEDED","result":{"transcription_url":"https://result.oss-cn-beijing.aliyuncs.com/result.json?signature=signed"}}})");
        if (step == failAt && !failureBody.isEmpty())
            body = failureBody;
        auto *reply = new Reply(request, body, step == failAt ? failStatus : 200, hang, this);
        if (step == failAt && failureNetwork != QNetworkReply::NoError)
            reply->networkError(failureNetwork);
        return reply;
    }
};
class AsrTest final : public QObject {
    Q_OBJECT
  private slots:
    void uploadFormEncoding();
    void completeWorkflow_data();
    void completeWorkflow();
    void failure_data();
    void failure();
    void cancellationAndDeadlines();
    void privateSettings();
    void diagnosticFields();
    void failureLogs();
    void modelValidation();
    void qwen3MissingResult();
    void backsUpTaskBeforePolling_data();
    void backsUpTaskBeforePolling();
    void rejectsInvalidTaskId();
};
static QString audioFile(const QTemporaryDir &directory)
{
    const auto path = directory.filePath("song.wav");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write("RIFF-test-audio");
    return path;
}
void AsrTest::uploadFormEncoding()
{
    QTemporaryDir directory;
    Manager manager;
    AliyunAsr asr(nullptr, &manager, 1, 1000, 5000);
    QSignalSpy complete(&asr, &AliyunAsr::completed);
    asr.start(audioFile(directory), "dummy-key", "ja");
    QTRY_COMPARE(complete.size(), 1);
    const auto type = manager.requests[1].header(QNetworkRequest::ContentTypeHeader).toByteArray();
    const QByteArray prefix("multipart/form-data; boundary=");
    QVERIFY(type.startsWith(prefix));
    const auto boundary = type.mid(prefix.size());
    // OSS rejects Qt's default quoted boundary before validating credentials.
    QVERIFY(!boundary.contains('"'));
    QVERIFY(QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$")).match(QString::fromLatin1(boundary)).hasMatch());
    const auto body = manager.bodies[1];
    const auto delimiter = "--" + boundary;
    QVERIFY(body.startsWith(delimiter + "\r\n"));
    QVERIFY(body.endsWith("\r\n" + delimiter + "--\r\n"));
    QCOMPARE(body.count(delimiter), 10); // Eight fields, one file, closing boundary.
    for (const auto &name : {"OSSAccessKeyId", "Signature", "policy", "key", "x-oss-object-acl",
                             "x-oss-forbid-overwrite", "success_action_status", "x-oss-content-type"}) {
        const auto field = QByteArray("form-data; name=\"") + name + "\"\r\n\r\n";
        QCOMPARE(body.count(field), 1);
        QVERIFY(body.indexOf(field) < body.indexOf("name=\"file\""));
    }
    const auto fileStart = body.indexOf("name=\"file\"");
    QVERIFY(fileStart >= 0);
    const auto dataStart = body.indexOf("\r\n\r\n", fileStart) + 4;
    const auto dataEnd = body.indexOf("\r\n" + delimiter, dataStart);
    QCOMPARE(body.mid(dataStart, dataEnd - dataStart), QByteArray("RIFF-test-audio"));
}
void AsrTest::completeWorkflow_data()
{
    QTest::addColumn<QString>("language");
    QTest::addColumn<QString>("model");
    for (const auto &model : {"fun-asr", "qwen-audio-3.1-asr-flash-filetrans", "qwen-audio-3.0-asr-flash-filetrans",
                             "qwen3-asr-flash-filetrans", "paraformer-v2"})
        for (const auto &language : {"ja", "en", "zh", "ko", "auto"})
            QTest::newRow(qPrintable(QString::fromLatin1(model) + QLatin1Char('-') + QLatin1String(language)))
                << QString::fromLatin1(language) << QString::fromLatin1(model);
}
void AsrTest::completeWorkflow()
{
    QFETCH(QString, language);
    QFETCH(QString, model);
    QTemporaryDir directory;
    Manager manager;
    AliyunAsr asr(nullptr, &manager, 1, 1000, 5000);
    QSignalSpy complete(&asr, &AliyunAsr::completed), failure(&asr, &AliyunAsr::failed);
    QSignalSpy submitted(&asr, &AliyunAsr::taskSubmitted);
    manager.qwen3Result = model == QStringLiteral("qwen3-asr-flash-filetrans");
    asr.start(audioFile(directory), "private-api-key", language, model);
    QTRY_COMPARE(complete.size(), 1);
    QCOMPARE(failure.size(), 0);
    QCOMPARE(complete.first().first().toByteArray(), manager.transcription);
    QCOMPARE(submitted.size(), 1);
    QCOMPARE(submitted.first(), (QVariantList{QStringLiteral("task-123"), model, language}));
    QCOMPARE(manager.requests.size(), 6);
    QVERIFY(!asr.active());
    QCOMPARE(manager.requests[0].url().path(), QStringLiteral("/api/v1/uploads"));
    QCOMPARE(manager.requests[0].rawHeader("Authorization"), QByteArray("Bearer private-api-key"));
    QVERIFY(manager.requests[1].rawHeader("Authorization").isEmpty());
    QVERIFY(manager.bodies[1].contains("RIFF-test-audio"));
    QVERIFY(manager.bodies[1].contains("name=\"file\""));
    QVERIFY(!manager.bodies[1].contains("private-api-key"));
    QCOMPARE(manager.requests[2].rawHeader("X-DashScope-Async"), QByteArray("enable"));
    QCOMPARE(manager.requests[2].rawHeader("X-DashScope-OssResourceResolve"), QByteArray("enable"));
    const auto submit = QJsonDocument::fromJson(manager.bodies[2]).object();
    QCOMPARE(submit.value("model").toString(), model);
    QCOMPARE(QUrlQuery(manager.requests[0].url()).queryItemValue("model"), model);
    const auto parameters = submit.value("parameters").toObject();
    const auto input = submit.value("input").toObject();
    QCOMPARE(parameters.value("channel_id").toArray(), QJsonArray{0});
    if (manager.qwen3Result) {
        QVERIFY(parameters.value("enable_words").toBool());
        QCOMPARE(parameters.value("enable_itn"), QJsonValue(false));
        QVERIFY(!parameters.contains("language_hints"));
        if (language == QStringLiteral("auto"))
            QVERIFY(!parameters.contains("language"));
        else
            QCOMPARE(parameters.value("language").toString(), language);
        QVERIFY(input.value("file_url").toString().startsWith("oss://tmp/test/"));
        QVERIFY(!input.contains("file_urls"));
    } else {
        QVERIFY(!parameters.contains("language"));
        QVERIFY(!parameters.contains("enable_words"));
        if (language == QStringLiteral("auto"))
            QVERIFY(!parameters.contains("language_hints"));
        else
            QCOMPARE(parameters.value("language_hints").toArray(), QJsonArray{language});
        QVERIFY(input.value("file_urls").toArray().first().toString().startsWith("oss://tmp/test/"));
        QVERIFY(!input.contains("file_url"));
    }
    QVERIFY(manager.requests.last().rawHeader("Authorization").isEmpty());
}
void AsrTest::backsUpTaskBeforePolling_data()
{
    QTest::addColumn<bool>("cancelAfterSubmit");
    QTest::newRow("cancel-after-submit") << true;
    QTest::newRow("poll-fails") << false;
}
void AsrTest::backsUpTaskBeforePolling()
{
    QFETCH(bool, cancelAfterSubmit);
    QTemporaryDir directory;
    const auto path = directory.filePath("backup.sqlite3");
    const QString connection = QStringLiteral("asr-lifecycle-backup-test");
    {
        SongStore store(path, connection);
        QVERIFY(store.getOrCreateSong("song-hash", "song.wav"));
        Manager manager;
        manager.failAt = 3;
        manager.failStatus = 500;
        AliyunAsr asr(nullptr, &manager, 1, 1000, 5000);
        bool saved = false;
        int requestsWhenSaved = 0;
        connect(&asr, &AliyunAsr::taskSubmitted, &asr,
                [&](const QString &taskId, const QString &model, const QString &language) {
                    saved = store.saveAsrTask(taskId, "song-hash", model, language);
                    requestsWhenSaved = manager.requests.size();
                    if (cancelAfterSubmit) asr.cancel();
                });
        QSignalSpy errors(&asr, &AliyunAsr::failed), complete(&asr, &AliyunAsr::completed);
        asr.start(audioFile(directory), "dummy-key", "ja");
        QTRY_VERIFY(saved);
        QCOMPARE(requestsWhenSaved, 3); // Policy, upload, submit; no poll yet.
        if (cancelAfterSubmit) {
            QTest::qWait(20);
            QCOMPARE(manager.requests.size(), 3);
            QCOMPARE(errors.size(), 0);
        } else {
            QTRY_COMPARE(errors.size(), 1);
        }
        QCOMPARE(complete.size(), 0);
        QVERIFY(!asr.active());
    }
    {
        SongStore restored(path, connection);
        QVERIFY(restored.isReady());
        QSqlQuery query(QSqlDatabase::database(connection));
        QVERIFY(query.exec("SELECT task_id, song_hash, model, language FROM asr_tasks"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("task-123"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("song-hash"));
        QCOMPARE(query.value(2).toString(), QStringLiteral("fun-asr"));
        QCOMPARE(query.value(3).toString(), QStringLiteral("ja"));
        QVERIFY(!query.next());
    }
}
void AsrTest::rejectsInvalidTaskId()
{
    QTemporaryDir directory;
    Manager manager;
    manager.invalidTaskId = true;
    AliyunAsr asr(nullptr, &manager, 1, 1000, 5000);
    QSignalSpy errors(&asr, &AliyunAsr::failed), submitted(&asr, &AliyunAsr::taskSubmitted);
    asr.start(audioFile(directory), "dummy-key", "ja");
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(errors.first().first().toString(), QStringLiteral("invalid_response"));
    QCOMPARE(submitted.size(), 0);
    QCOMPARE(manager.requests.size(), 3);
}
void AsrTest::modelValidation()
{
    QTemporaryDir directory;
    Manager manager;
    AliyunAsr asr(nullptr, &manager, 1, 1000, 5000);
    QSignalSpy errors(&asr, &AliyunAsr::failed);
    for (const auto &model : {"", "unknown", "fun-asr&model=unknown"}) {
        asr.start(audioFile(directory), "dummy-key", "ja", QString::fromLatin1(model));
        QCOMPARE(errors.takeFirst().first().toString(), QStringLiteral("unsupported_model"));
        QVERIFY(manager.requests.isEmpty());
        QVERIFY(!asr.active());
    }
    QSignalSpy complete(&asr, &AliyunAsr::completed);
    asr.start(audioFile(directory), "dummy-key", "ja");
    QTRY_COMPARE(complete.size(), 1);
    QCOMPARE(QUrlQuery(manager.requests[0].url()).queryItemValue("model"), QStringLiteral("fun-asr"));
    QCOMPARE(QJsonDocument::fromJson(manager.bodies[2]).object().value("model").toString(), QStringLiteral("fun-asr"));
}
void AsrTest::qwen3MissingResult()
{
    QTemporaryDir directory;
    Manager manager;
    manager.qwen3Result = true;
    manager.missingResult = true;
    AliyunAsr asr(nullptr, &manager, 1, 1000, 5000);
    QSignalSpy errors(&asr, &AliyunAsr::failed);
    asr.start(audioFile(directory), "dummy-key", "ja", "qwen3-asr-flash-filetrans");
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(errors.first().first().toString(), QStringLiteral("invalid_response"));
    QCOMPARE(manager.requests.size(), 5);
}
void AsrTest::failure_data()
{
    QTest::addColumn<int>("step");
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("expected");
    QTest::newRow("auth") << 0 << 401 << QStringLiteral("unauthorized");
    QTest::newRow("upload") << 1 << 500 << QStringLiteral("network");
    QTest::newRow("submit-rate") << 2 << 429 << QStringLiteral("rate_limited");
    QTest::newRow("poll") << 3 << 500 << QStringLiteral("network");
    QTest::newRow("subtask") << -2 << 200 << QStringLiteral("task_failed");
    QTest::newRow("policy") << -3 << 200 << QStringLiteral("invalid_response");
    QTest::newRow("result") << -4 << 200 << QStringLiteral("invalid_asr");
    QTest::newRow("oversized-result") << -5 << 200 << QStringLiteral("invalid_response");
}
void AsrTest::failure()
{
    QFETCH(int, step);
    QFETCH(int, status);
    QFETCH(QString, expected);
    QTemporaryDir directory;
    Manager manager;
    manager.failAt = step;
    manager.failStatus = status;
    manager.subtaskFails = step == -2;
    manager.invalidPolicy = step == -3;
    if (step == -4)
        manager.transcription = "{}";
    if (step == -5)
        manager.transcription = QByteArray(2 * 1024 * 1024 + 1, 'x');
    AliyunAsr asr(nullptr, &manager, 1, 1000, 5000);
    QSignalSpy errors(&asr, &AliyunAsr::failed), complete(&asr, &AliyunAsr::completed);
    asr.start(audioFile(directory), "key", "auto");
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(errors.first().first().toString(), expected);
    QCOMPARE(complete.size(), 0);
    QVERIFY(!asr.active());
}
void AsrTest::cancellationAndDeadlines()
{
    QTemporaryDir directory;
    Manager manager;
    manager.hang = true;
    AliyunAsr asr(nullptr, &manager, 1, 20, 40);
    QSignalSpy errors(&asr, &AliyunAsr::failed), complete(&asr, &AliyunAsr::completed);
    const auto path = audioFile(directory);
    asr.start(path, "key", "auto");
    asr.cancel();
    QTest::qWait(60);
    QCOMPARE(errors.size(), 0);
    QCOMPARE(complete.size(), 0);
    QCOMPARE(manager.requests.size(), 1);
    asr.start(path, "key", "auto");
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(errors.first().first().toString(), QStringLiteral("timeout"));
    errors.clear();
    asr.start(path, {}, "auto");
    QCOMPARE(errors.first().first().toString(), QStringLiteral("missing_key"));
}
namespace {
class LogCapture {
  public:
    LogCapture()
    {
        current = this;
        previous =
            qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &message) {
                if (QByteArray(context.category) == "nekotune.asr")
                    current->messages.append(message);
                else if (current->previous)
                    current->previous(type, context, message);
            });
    }
    ~LogCapture()
    {
        qInstallMessageHandler(previous);
        current = nullptr;
    }
    QStringList messages;

  private:
    QtMessageHandler previous;
    static inline LogCapture *current = nullptr;
};
} // namespace
void AsrTest::diagnosticFields()
{
    QNetworkRequest request(QUrl("https://private.example/result?signature=secret-url"));
    Reply reply(request, {}, 403, true, nullptr);
    reply.networkError(QNetworkReply::ContentAccessDenied);
    const auto json = networkDiagnostics(
        &reply, R"({"code":"InvalidApiKey","request_id":"request-123","message":"private-api-key secret-url"})",
        "private-api-key");
    QVERIFY(json.contains("http=403"));
    QVERIFY(json.contains("network=ContentAccessDenied"));
    QVERIFY(json.contains("upstream_code=InvalidApiKey"));
    QVERIFY(json.contains("request_id=request-123"));
    QVERIFY(!json.contains("private-api-key"));
    QVERIFY(!json.contains("secret-url"));
    const auto xml = networkDiagnostics(&reply, "<Error><Code>AccessDenied</Code><RequestId>oss-123</"
                                                "RequestId><Message>secret-url</Message></Error>");
    QVERIFY(xml.contains("upstream_code=AccessDenied"));
    QVERIFY(xml.contains("request_id=oss-123"));
    QVERIFY(!xml.contains("secret-url"));
    const auto unsafe = providerDiagnostics({{"code", "private-api-key"}, {"request_id", "https://private.example"}},
                                            "private-api-key");
    QCOMPARE(unsafe, QStringLiteral("upstream_code=unavailable request_id=unavailable"));
}
void AsrTest::failureLogs()
{
    QTemporaryDir directory;
    Manager manager;
    manager.failAt = 5;
    manager.failStatus = 0;
    manager.failureBody = R"({"code":"DownloadFailed","request_id":"req-123","message":"private-api-key secret-url"})";
    manager.failureNetwork = QNetworkReply::HostNotFoundError;
    AliyunAsr asr(nullptr, &manager, 1, 1000, 5000);
    QSignalSpy errors(&asr, &AliyunAsr::failed);
    LogCapture capture;
    const auto path = audioFile(directory);
    asr.start(path, "private-api-key", "ja");
    QTRY_COMPARE(errors.size(), 1);
    const auto log = capture.messages.join('\n');
    for (const auto &stage : {"get_policy", "upload", "submit", "poll", "download"})
        QVERIFY2(log.contains(QStringLiteral("stage=") + QLatin1String(stage)), qPrintable(log));
    QVERIFY(log.contains("network=HostNotFoundError"));
    QVERIFY(log.contains("http=0"));
    QVERIFY(log.contains("upstream_code=DownloadFailed"));
    QVERIFY(log.contains("request_id=req-123"));
    QVERIFY(log.contains("failed stage=download reason=network"));
    QVERIFY(log.contains("elapsed_ms="));
    for (const auto &secret :
         {"private-api-key", "secret-url", "signature", "RIFF-test-audio", "original-url", "hello"})
        QVERIFY2(!log.contains(QLatin1String(secret)), qPrintable(log));
    QVERIFY(!log.contains(path));
    QVERIFY(!log.contains("canceled"));
}
void AsrTest::privateSettings()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("settings.json");
    QFile legacy(path);
    QVERIFY(legacy.open(QIODevice::WriteOnly));
    legacy.write(R"({"api_key":"old-key","language":"en"})");
    legacy.close();
    AsrSettings settings(path);
    QCOMPARE(settings.apiKey(), QStringLiteral("old-key"));
    QVERIFY(!settings.publicSettings().contains("language"));
    QString error;
    QVERIFY(settings.update({{"api_key", "secret-key"}}, &error));
    QVERIFY(!QJsonDocument(settings.publicSettings()).toJson().contains("secret-key"));
    QVERIFY(settings.publicSettings().value("api_key_configured").toBool());
    QVERIFY(legacy.open(QIODevice::ReadOnly));
    QVERIFY(!QJsonDocument::fromJson(legacy.readAll()).object().contains("language"));
    legacy.close();
    QVERIFY(!(QFile::permissions(path) &
              (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther)));
    AsrSettings restored(path);
    QCOMPARE(restored.apiKey(), QStringLiteral("secret-key"));
    QVERIFY(restored.update({}, &error));
    QCOMPARE(restored.apiKey(), QStringLiteral("secret-key"));
    QVERIFY(!restored.update({{"api_key", "bad\nheader"}}, &error));
    QCOMPARE(restored.apiKey(), QStringLiteral("secret-key"));
    QVERIFY(restored.update({{"api_key", ""}}, &error));
    QVERIFY(AsrSettings(path).apiKey().isEmpty());
    AsrSettings impossible(directory.filePath("settings.json/blocked"));
    QVERIFY(!impossible.update({{"api_key", "key"}}, &error));
    QVERIFY(impossible.apiKey().isEmpty());
}
QTEST_GUILESS_MAIN(AsrTest)
#include "aliyun_asr_test.moc"
