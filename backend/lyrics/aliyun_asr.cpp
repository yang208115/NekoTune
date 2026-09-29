#include "lyrics/aliyun_asr.h"
#include "lyrics/asr_parser.h"
#include "lyrics/network_diagnostics.h"
#include <QLoggingCategory>
#include <QSslError>

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QUuid>

namespace nekotune {
Q_LOGGING_CATEGORY(asrLog, "nekotune.asr")
namespace {
constexpr qint64 maxResponse = 2 * 1024 * 1024;
QUrl storageUrl(const QString &value)
{
    QUrl url(value);
    if (!url.isValid() || !url.host().endsWith(QStringLiteral(".aliyuncs.com")) || !url.userInfo().isEmpty() ||
        (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")))
        return {};
    url.setScheme(QStringLiteral("https"));
    return url;
}
} // namespace
AliyunAsr::AliyunAsr(QObject *parent, QNetworkAccessManager *manager, int pollIntervalMs, int requestTimeoutMs,
                     int totalTimeoutMs)
    : QObject(parent), m_manager(manager ? manager : new QNetworkAccessManager(this)),
      m_requestTimeoutMs(requestTimeoutMs), m_totalTimeoutMs(totalTimeoutMs)
{
    m_pollTimer.setSingleShot(true);
    m_pollTimer.setInterval(pollIntervalMs);
    m_totalTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &AliyunAsr::poll);
    connect(&m_totalTimer, &QTimer::timeout, this, [this]() { fail(QStringLiteral("timeout")); });
}
AliyunAsr::~AliyunAsr()
{
    cancel();
}
void AliyunAsr::cancel()
{
    if (m_active)
        qCInfo(asrLog).noquote() << "operation=" + m_operationId << "canceled stage=" + m_stage
                                 << "elapsed_ms=" << m_elapsed.elapsed();
    m_active = false;
    m_pollTimer.stop();
    m_totalTimer.stop();
    m_key.clear();
    m_task.clear();
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply.clear();
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}
void AliyunAsr::fail(const QString &code)
{
    qCWarning(asrLog).noquote() << "operation=" + m_operationId << "failed stage=" + m_stage << "reason=" + code
                                << "elapsed_ms=" << (m_elapsed.isValid() ? m_elapsed.elapsed() : 0)
                                << (m_reply ? networkDiagnostics(m_reply) : QString{});
    m_active = false;
    cancel();
    emit failed(code);
}
QNetworkRequest AliyunAsr::apiRequest(const QString &path) const
{
    QNetworkRequest request(QUrl(QStringLiteral("https://dashscope.aliyuncs.com/api/v1/") + path));
    request.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(m_requestTimeoutMs);
    return request;
}
void AliyunAsr::watch(QNetworkReply *reply, const QString &stage, std::function<void(const QByteArray &)> next)
{
    m_reply = reply;
    m_stage = stage;
    QElapsedTimer elapsed;
    elapsed.start();
    if (stage != QStringLiteral("poll"))
        qCInfo(asrLog).noquote() << "operation=" + m_operationId << "request stage=" + stage;
    connect(reply, &QNetworkReply::sslErrors, this, [this, stage](const QList<QSslError> &errors) {
        QList<int> codes;
        for (const auto &error : errors)
            codes.append(static_cast<int>(error.error()));
        qCWarning(asrLog).noquote() << "operation=" + m_operationId << "TLS failure stage=" + stage
                                    << "ssl_codes=" << codes;
    });
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, this, [this, reply]() {
        if (m_reply == reply)
            fail(QStringLiteral("timeout"));
    });
    deadline->start(m_requestTimeoutMs);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (m_reply == reply && reply->bytesAvailable() > maxResponse)
            fail(QStringLiteral("invalid_response"));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, stage, elapsed, next = std::move(next)]() {
        if (m_reply != reply || !m_active)
            return;
        m_reply.clear();
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            qCWarning(asrLog).noquote() << "operation=" + m_operationId << "response stage=" + stage
                                        << "elapsed_ms=" << elapsed.elapsed()
                                        << networkDiagnostics(reply, bytes, m_key);
        } else if (stage != QStringLiteral("poll")) {
            qCInfo(asrLog).noquote() << "operation=" + m_operationId << "response stage=" + stage
                                     << "elapsed_ms=" << elapsed.elapsed() << networkDiagnostics(reply);
        }
        if (status == 401 || status == 403) {
            fail(QStringLiteral("unauthorized"));
            return;
        }
        if (status == 429) {
            fail(QStringLiteral("rate_limited"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            fail(QStringLiteral("network"));
            return;
        }
        if (bytes.size() > maxResponse) {
            fail(QStringLiteral("invalid_response"));
            return;
        }
        next(bytes);
    });
}
bool AliyunAsr::supportsModel(const QString &model)
{
    return QStringList{QStringLiteral("fun-asr"), QStringLiteral("qwen-audio-3.1-asr-flash-filetrans"),
                       QStringLiteral("qwen-audio-3.0-asr-flash-filetrans"), QStringLiteral("qwen3-asr-flash-filetrans"),
                       QStringLiteral("paraformer-v2")}.contains(model);
}
void AliyunAsr::start(const QString &path, const QString &apiKey, const QString &language, const QString &model)
{
    cancel();
    m_operationId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    m_stage = QStringLiteral("validate");
    m_lastTaskState.clear();
    m_elapsed.start();
    if (!supportsModel(model)) {
        fail(QStringLiteral("unsupported_model"));
        return;
    }
    m_model = model;
    qCInfo(asrLog).noquote() << "operation=" + m_operationId << "start model=" + m_model;
    if (apiKey.isEmpty()) {
        fail(QStringLiteral("missing_key"));
        return;
    }
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable() || file.size() <= 0 || file.size() > 512LL * 1024 * 1024) {
        fail(QStringLiteral("audio_file"));
        return;
    }
    m_path = path;
    m_key = apiKey;
    m_language = language;
    m_active = true;
    m_totalTimer.start(m_totalTimeoutMs);
    emit progress(QStringLiteral("uploading"));
    watch(m_manager->get(apiRequest(QStringLiteral("uploads?action=getPolicy&model=") + m_model)),
          QStringLiteral("get_policy"), [this](const QByteArray &bytes) {
              const auto policy = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("data")).toObject();
              if (policy.isEmpty()) {
                  fail(QStringLiteral("invalid_response"));
                  return;
              }
              upload(policy);
          });
}
void AliyunAsr::upload(const QJsonObject &policy)
{
    const auto url = storageUrl(policy.value(QStringLiteral("upload_host")).toString());
    for (const auto &key :
         {"upload_dir", "oss_access_key_id", "signature", "policy", "x_oss_object_acl", "x_oss_forbid_overwrite"}) {
        if (policy.value(QLatin1String(key)).toString().isEmpty()) {
            fail(QStringLiteral("invalid_response"));
            return;
        }
    }
    if (url.isEmpty()) {
        fail(QStringLiteral("invalid_response"));
        return;
    }
    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multi->setBoundary("nekotune-" + QUuid::createUuid().toByteArray(QUuid::WithoutBraces));
    auto field = [multi](const QByteArray &name, const QString &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"" + name + "\"");
        part.setBody(value.toUtf8());
        multi->append(part);
    };
    const QString filename =
        QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char('.') + QFileInfo(m_path).suffix();
    const QString key = policy.value(QStringLiteral("upload_dir")).toString() + QLatin1Char('/') + filename;
    field("OSSAccessKeyId", policy.value(QStringLiteral("oss_access_key_id")).toString());
    field("Signature", policy.value(QStringLiteral("signature")).toString());
    field("policy", policy.value(QStringLiteral("policy")).toString());
    field("x-oss-object-acl", policy.value(QStringLiteral("x_oss_object_acl")).toString());
    field("x-oss-forbid-overwrite", policy.value(QStringLiteral("x_oss_forbid_overwrite")).toString());
    field("key", key);
    field("success_action_status", QStringLiteral("200"));
    const auto mime = QMimeDatabase().mimeTypeForFile(m_path).name();
    field("x-oss-content-type", mime);
    auto *file = new QFile(m_path, multi);
    if (!file->open(QIODevice::ReadOnly)) {
        delete multi;
        fail(QStringLiteral("audio_file"));
        return;
    }
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(filename));
    part.setHeader(QNetworkRequest::ContentTypeHeader, mime);
    part.setBodyDevice(file);
    multi->append(part);
    QNetworkRequest request(url);
    // OSS rejects Qt's quoted boundary parameter. Keep the same token in the header and body.
    request.setHeader(QNetworkRequest::ContentTypeHeader, "multipart/form-data; boundary=" + multi->boundary());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_manager->post(request, multi);
    multi->setParent(reply);
    watch(reply, QStringLiteral("upload"), [this, key](const QByteArray &) { submit(QStringLiteral("oss://") + key); });
}
void AliyunAsr::submit(const QString &ossUrl)
{
    emit progress(QStringLiteral("submitting"));
    auto request = apiRequest(QStringLiteral("services/audio/asr/transcription"));
    request.setRawHeader("X-DashScope-Async", "enable");
    request.setRawHeader("X-DashScope-OssResourceResolve", "enable");
    const bool qwen3 = m_model == QStringLiteral("qwen3-asr-flash-filetrans");
    QJsonObject parameters{{QStringLiteral("channel_id"), QJsonArray{0}}};
    QJsonObject input;
    if (qwen3) {
        input.insert(QStringLiteral("file_url"), ossUrl);
        parameters.insert(QStringLiteral("enable_words"), true);
        parameters.insert(QStringLiteral("enable_itn"), false);
        if (m_language != QStringLiteral("auto"))
            parameters.insert(QStringLiteral("language"), m_language);
    } else {
        input.insert(QStringLiteral("file_urls"), QJsonArray{ossUrl});
        if (m_language != QStringLiteral("auto"))
            parameters.insert(QStringLiteral("language_hints"), QJsonArray{m_language});
    }
    const QJsonObject body{{QStringLiteral("model"), m_model}, {QStringLiteral("input"), input},
                           {QStringLiteral("parameters"), parameters}};
    watch(m_manager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)), QStringLiteral("submit"),
          [this](const QByteArray &bytes) {
              m_task = QJsonDocument::fromJson(bytes)
                           .object()
                           .value(QStringLiteral("output"))
                           .toObject()
                           .value(QStringLiteral("task_id"))
                           .toString();
              if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,128}$")).match(m_task).hasMatch()) {
                  fail(QStringLiteral("invalid_response"));
                  return;
              }
              emit taskSubmitted(m_task, m_model, m_language);
              if (!m_active) return;
              emit progress(QStringLiteral("recognizing"));
              m_pollTimer.start();
          });
}
void AliyunAsr::poll()
{
    watch(m_manager->get(apiRequest(QStringLiteral("tasks/") + m_task)), QStringLiteral("poll"),
          [this](const QByteArray &bytes) {
              const auto output = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("output")).toObject();
              const auto state = output.value(QStringLiteral("task_status")).toString();
              if (state != m_lastTaskState) {
                  static const QStringList knownStates{QStringLiteral("PENDING"),   QStringLiteral("RUNNING"),
                                                       QStringLiteral("SUCCEEDED"), QStringLiteral("FAILED"),
                                                       QStringLiteral("CANCELED"),  QStringLiteral("UNKNOWN")};
                  qCInfo(asrLog).noquote()
                      << "operation=" + m_operationId
                      << "stage=poll task_state=" << (knownStates.contains(state) ? state : QStringLiteral("INVALID"));
                  m_lastTaskState = state;
              }
              if (state == QStringLiteral("PENDING") || state == QStringLiteral("RUNNING")) {
                  m_pollTimer.start();
                  return;
              }
              if (state == QStringLiteral("FAILED") || state == QStringLiteral("CANCELED") ||
                  state == QStringLiteral("UNKNOWN")) {
                  qCWarning(asrLog).noquote() << "operation=" + m_operationId << providerDiagnostics(output, m_key);
                  fail(QStringLiteral("task_failed"));
                  return;
              }
              if (state != QStringLiteral("SUCCEEDED")) {
                  fail(QStringLiteral("invalid_response"));
                  return;
              }
              QJsonObject result;
              if (m_model == QStringLiteral("qwen3-asr-flash-filetrans")) {
                  result = output.value(QStringLiteral("result")).toObject();
              } else {
                  const auto results = output.value(QStringLiteral("results")).toArray();
                  if (results.isEmpty()) {
                      fail(QStringLiteral("invalid_response"));
                      return;
                  }
                  result = results.first().toObject();
                  if (result.value(QStringLiteral("subtask_status")).toString() != QStringLiteral("SUCCEEDED")) {
                      qCWarning(asrLog).noquote() << "operation=" + m_operationId << providerDiagnostics(result, m_key);
                      fail(QStringLiteral("task_failed"));
                      return;
                  }
              }
              const auto url = storageUrl(result.value(QStringLiteral("transcription_url")).toString());
              if (url.isEmpty()) {
                  fail(QStringLiteral("invalid_response"));
                  return;
              }
              emit progress(QStringLiteral("downloading"));
              QNetworkRequest request(url);
              request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
              watch(m_manager->get(request), QStringLiteral("download"), [this](const QByteArray &json) {
                  if (AsrParser::parse(json).isEmpty()) {
                      fail(QStringLiteral("invalid_asr"));
                      return;
                  }
                  qCInfo(asrLog).noquote()
                      << "operation=" + m_operationId << "completed elapsed_ms=" << m_elapsed.elapsed();
                  m_active = false;
                  cancel();
                  emit completed(json);
              });
          });
}
} // namespace nekotune
