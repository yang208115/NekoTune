#include "lyrics/lrclib_provider.h"
#include "lyrics/network_diagnostics.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>
#include <cmath>

namespace nekotune {
Q_LOGGING_CATEGORY(lyricsLog, "nekotune.lyrics")

LrclibProvider::LrclibProvider(QObject *parent, QNetworkAccessManager *manager, int deadlineMs)
    : LyricsProvider(parent), m_manager(manager ? manager : new QNetworkAccessManager(this)), m_deadlineMs(deadlineMs)
{
}

LrclibProvider::~LrclibProvider()
{
    cancel();
}

void LrclibProvider::cancel()
{
    if (!m_reply)
        return;
    auto *reply = m_reply.data();
    m_reply.clear();
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void LrclibProvider::request(const LyricsQuery &query, quint64 token, bool search)
{
    cancel();
    QUrl url(search ? QStringLiteral("https://lrclib.net/api/search") : QStringLiteral("https://lrclib.net/api/get"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("track_name"), query.title.simplified());
    if (!query.artist.simplified().isEmpty())
        params.addQueryItem(QStringLiteral("artist_name"), query.artist.simplified());
    // Search broadly enough to present alternative releases; album is used when
    // ranking.
    if (!search) {
        params.addQueryItem(QStringLiteral("album_name"), query.album.simplified());
        if (query.durationMs >= 1000 && query.durationMs <= 3600000)
            params.addQueryItem(QStringLiteral("duration"), QString::number(query.durationMs / 1000.0, 'f', 3));
    }
    url.setQuery(params);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NekoTune/0.1.0 (desktop music player)"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(5000);
    const QString stage = search ? QStringLiteral("search") : QStringLiteral("get");
    QElapsedTimer elapsed;
    elapsed.start();
    qCInfo(lyricsLog).noquote() << "LRCLIB request token=" << token << "stage=" + stage;
    auto *reply = m_manager->get(request);
    m_reply = reply;
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("lyrics_timeout", true);
        reply->abort();
    });
    deadline->start(m_deadlineMs);
    connect(reply, &QNetworkReply::readyRead, this, [reply]() {
        if (reply->bytesAvailable() > 2 * 1024 * 1024) {
            reply->setProperty("lyrics_oversize", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, deadline, token, search, stage, elapsed]() {
        deadline->stop();
        reply->deleteLater();
        if (m_reply != reply)
            return;
        m_reply.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto logFailure = [&](const QString &reason) {
            qCWarning(lyricsLog).noquote() << "LRCLIB failed token=" << token << "stage=" + stage << "reason=" + reason
                                           << "elapsed_ms=" << elapsed.elapsed() << networkDiagnostics(reply);
        };
        QString failure;
        if (reply->property("lyrics_timeout").toBool() || reply->error() == QNetworkReply::TimeoutError)
            failure = QStringLiteral("timeout");
        else if (reply->property("lyrics_oversize").toBool())
            failure = QStringLiteral("invalid_response");
        else if (status == 404)
            failure = QStringLiteral("not_found");
        else if (status == 429)
            failure = QStringLiteral("rate_limited");
        else if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
            failure = QStringLiteral("network");
        if (!failure.isEmpty()) {
            logFailure(failure);
            emit failed(token, failure);
            return;
        }
        const auto bytes = reply->readAll();
        const auto json = QJsonDocument::fromJson(bytes);
        if (bytes.size() > 2 * 1024 * 1024 || (search ? !json.isArray() : !json.isObject())) {
            logFailure(QStringLiteral("invalid_response"));
            emit failed(token, QStringLiteral("invalid_response"));
            return;
        }
        const auto array = search ? json.array() : QJsonArray{json.object()};
        QVector<LyricsCandidate> candidates;
        bool invalid = false;
        for (const auto &value : array) {
            const auto object = value.toObject();
            const auto synced = object.value(QStringLiteral("syncedLyrics"));
            const auto plain = object.value(QStringLiteral("plainLyrics"));
            const double duration = object.value(QStringLiteral("duration")).toDouble(-1);
            if (!value.isObject() || !object.value(QStringLiteral("id")).isDouble() ||
                !object.value(QStringLiteral("trackName")).isString() ||
                !object.value(QStringLiteral("artistName")).isString() ||
                !object.value(QStringLiteral("albumName")).isString() ||
                !object.value(QStringLiteral("instrumental")).isBool() || !std::isfinite(duration) || duration < 0 ||
                duration > 86400 || !(synced.isString() || synced.isNull() || synced.isUndefined()) ||
                !(plain.isString() || plain.isNull() || plain.isUndefined())) {
                invalid = true;
                continue;
            }
            LyricsDocument document;
            document.source = QStringLiteral("lrclib");
            document.providerId = object.value(QStringLiteral("id")).toInteger();
            document.matched = {object.value(QStringLiteral("trackName")).toString(),
                                object.value(QStringLiteral("artistName")).toString(),
                                object.value(QStringLiteral("albumName")).toString(), qRound64(duration * 1000)};
            document.syncedLyrics = synced.toString();
            document.plainLyrics = plain.toString();
            document.instrumental = object.value(QStringLiteral("instrumental")).toBool();
            document.validate();
            if (!document.isEmpty())
                candidates.append({document});
            else if (!synced.toString().trimmed().isEmpty())
                invalid = true;
        }
        if (candidates.isEmpty() && invalid) {
            logFailure(QStringLiteral("invalid_response"));
            emit failed(token, QStringLiteral("invalid_response"));
        } else {
            qCInfo(lyricsLog).noquote() << "LRCLIB completed token=" << token << "stage=" + stage
                                        << "elapsed_ms=" << elapsed.elapsed() << "candidates=" << candidates.size();
            emit completed(token, candidates);
        }
    });
}

} // namespace nekotune
