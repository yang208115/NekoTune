#include "infrastructure/kugou/kugou_api_client.h"
#include "infrastructure/kugou/kugou_endpoints.h"
#include <memory>

#include "domain/lyrics/krc_parser.h"
#include "domain/lyrics/lrc_parser.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

#include <limits>

namespace nekotune {
constexpr qint64 kMaxJsonBytes = 4LL * 1024 * 1024;
void KugouApiClient::requestJson(const QString &route, const QJsonObject &body, JsonCallback callback) {
    m_cancelled = false;
    QNetworkRequest request(kugouRequestUrl(m_baseUrl, route));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "Mozilla/5.0 NekoTune/1.0");
    QNetworkReply *reply = nullptr;
    if (body.isEmpty()) {
        reply = m_manager->get(request);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("X-Account-Key", m_account.key.toUtf8());
        reply = m_manager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    m_reply = reply;
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes]() {
        bytes->append(reply->readAll());
        if (bytes->size() > kMaxJsonBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, callback = std::move(callback)]() {
        if (m_reply == reply)
            m_reply = nullptr;
        bytes->append(reply->readAll());
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto json = QJsonDocument::fromJson(*bytes);
        const auto object = json.isObject() ? json.object() : QJsonObject{};
        QString error;
        if (m_cancelled)
            error = QStringLiteral("Cancelled");
        else if (bytes->size() > kMaxJsonBytes)
            error = QStringLiteral("Worker response too large");
        else if (http >= 300 && http < 400)
            error = QStringLiteral("Worker redirected unexpectedly");
        else if (http >= 400)
            error = object.value(QStringLiteral("error_msg"))
                        .toString(QStringLiteral("Worker HTTP %1").arg(http));
        else if (reply->error() != QNetworkReply::NoError)
            error = QStringLiteral("Cannot reach Kugou Worker");
        else if (!json.isObject())
            error = QStringLiteral("Invalid Worker response");
        reply->deleteLater();
        callback(object, error);
    });
}

void KugouApiClient::accountRequest(const QString &route, const QJsonObject &body, JsonCallback callback) {
    QJsonObject request = body;
    QJsonObject cookies;
    for (auto it = m_account.cookies.cbegin(); it != m_account.cookies.cend(); ++it)
        cookies.insert(it.key(), it.value());
    request.insert(QStringLiteral("cookies"), cookies);
    requestJson(route, request,
                [this, callback = std::move(callback)](QJsonObject result, const QString &error) {
                    QString failure = error;
                    // Cookies are backend-private; strip them before forwarding account results.
                    const auto updated = result.take(QStringLiteral("cookies"));
                    if (failure.isEmpty() && updated.isObject()) {
                        const auto values = updated.toObject();
                        const auto previous = m_account.cookies;
                        for (auto it = values.begin(); it != values.end(); ++it)
                            if (it.value().isString())
                                m_account.cookies.insert(it.key(), it.value().toString());
                        if (!m_account.saveSession()) {
                            // Do not expose a successful session update that cannot survive restart.
                            m_account.cookies = previous;
                            failure = m_account.sessionError;
                        }
                    }
                    callback(result, failure);
                });
}
// Cancellation deliberately disconnects the normal reply callback.
// The admitted download/service operation supplies its terminal outcome.
// This avoids abort() triggering a second failure/completion sequence.
// Keep deferred deletion so no callback frees its active reply stack.
void KugouApiClient::cancel() {
    m_cancelled = true;
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}
} // namespace nekotune
