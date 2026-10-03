#include "infrastructure/kugou/krc_response.h"
#include "infrastructure/kugou/kugou_settings.h"
#include "infrastructure/lyrics/kugou_provider.h"
#include "domain/lyrics/krc_parser.h"
#include "domain/lyrics/lrc_parser.h"
#include "infrastructure/lyrics/network_diagnostics.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <cmath>

namespace nekotune {
Q_LOGGING_CATEGORY(kugouLog, "nekotune.lyrics.kugou")

namespace {
// Search fields can contain provider-added emphasis markup.
// Remove that decoration before matching or plain-text presentation.
// Decode the common entities without treating the value as rich text.
// The result remains untrusted descriptive data, not executable markup.
QString cleanText(QString value) {
    static const QRegularExpression emphasis(QStringLiteral("</?em\\b[^>]*>"),
                                             QRegularExpression::CaseInsensitiveOption);
    value.remove(emphasis);
    return value.replace(QStringLiteral("&amp;"), QStringLiteral("&"))
        .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
        .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
        .replace(QStringLiteral("&quot;"), QStringLiteral("\""));
}

// Provider IDs can arrive as strings or JSON numeric values.
// Accept only finite representable numeric values on the numeric path.
// The upper bound avoids identities already rounded by JSON doubles.
// Invalid values use zero and are filtered by the caller's rules.
qint64 integer(const QJsonValue &value) {
    if (value.isString()) {
        bool ok = false;
        const auto number = value.toString().toLongLong(&ok);
        return ok ? number : 0;
    }
    const double number = value.toDouble();
    return std::isfinite(number) && number >= 0 && number <= 9007199254740991.0 ? qint64(number) : 0;
}

QString coverUrl(const QString &image) {
    const QUrl url(image.trimmed().replace(QStringLiteral("{size}"), QStringLiteral("240")));
    if ((url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) ||
        url.host().compare(QStringLiteral("imge.kugou.com"), Qt::CaseInsensitive) != 0)
        return {};
    QUrl secure = url;
    secure.setScheme(QStringLiteral("https"));
    return secure.toString();
}
} // namespace

KugouProvider::KugouProvider(QObject *parent, QNetworkAccessManager *manager, const QUrl &baseUrl)
    : LyricsProvider(parent), m_manager(manager ? manager : new QNetworkAccessManager(this)),
      m_baseUrl(baseUrl), m_useSettings(baseUrl.isEmpty()) {}

bool KugouProvider::configurationAvailable() const {
    if (!m_useSettings)
        return true;
    const auto settings = KugouSettings::load();
    return settings.unavailableReason().isEmpty() && settings.workerUrl == m_baseUrl;
}

KugouProvider::~KugouProvider() { cancel(); }

void KugouProvider::cancel() {
    if (!m_reply)
        return;
    auto *reply = m_reply.data();
    m_reply.clear();
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void KugouProvider::get(const QString &path, const QUrlQuery &params, quint64 token,
                        const std::function<void(const QJsonObject &)> &onSuccess,
                        const std::function<void(const QString &)> &onFailure) {
    cancel();
    if (!configurationAvailable()) {
        emit failed(token, QStringLiteral("kugou_unavailable"));
        return;
    }
    QUrl url = m_baseUrl.resolved(QUrl(path));
    url.setQuery(params);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("NekoTune/0.1.0 (desktop music player)"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(20000);
    auto *reply = m_manager->get(request);
    m_reply = reply;
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("lyrics_timeout", true);
        reply->abort();
    });
    deadline->start(20000);
    connect(reply, &QNetworkReply::readyRead, this, [reply]() {
        if (reply->bytesAvailable() > 4 * 1024 * 1024) {
            reply->setProperty("lyrics_oversize", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, deadline, token, path, onSuccess, onFailure]() {
                deadline->stop();
                reply->deleteLater();
                if (m_reply != reply)
                    return;
                m_reply.clear();
                if (!configurationAvailable()) {
                    emit failed(token, QStringLiteral("kugou_unavailable"));
                    return;
                }
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QString failure;
                if (reply->property("lyrics_timeout").toBool() ||
                    reply->error() == QNetworkReply::TimeoutError || status == 504)
                    failure = QStringLiteral("timeout");
                else if (reply->property("lyrics_oversize").toBool())
                    failure = QStringLiteral("invalid_response");
                else if (status == 429)
                    failure = QStringLiteral("rate_limited");
                else if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
                    failure = QStringLiteral("network");
                if (!failure.isEmpty()) {
                    qCWarning(kugouLog).noquote()
                        << "Kugou request failed" << path << failure << networkDiagnostics(reply);
                    if (onFailure)
                        onFailure(failure);
                    else
                        emit failed(token, failure);
                    return;
                }
                const auto bytes = reply->readAll();
                const auto json = QJsonDocument::fromJson(bytes);
                if (bytes.size() > 4 * 1024 * 1024 || !json.isObject()) {
                    if (onFailure)
                        onFailure(QStringLiteral("invalid_response"));
                    else
                        emit failed(token, QStringLiteral("invalid_response"));
                    return;
                }
                onSuccess(json.object());
            });
}

// Discard resolution handles from the previous song search.
// The UI will receive a new candidate revision for this result set.
// Expand grouped releases while deduplicating their provider hashes.
// Fallback labels/artwork come from the group's parent when missing.
// Bound candidate counts so a remote response cannot grow UI state freely.
void KugouProvider::request(const LyricsQuery &query, quint64 token, bool) {
    m_resolutions.clear();
    if (m_useSettings)
        m_baseUrl = KugouSettings::load().workerUrl;
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("keywords"), query.title.simplified());
    params.addQueryItem(QStringLiteral("type"), QStringLiteral("song"));
    params.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    get(QStringLiteral("/search"), params, token, [this, token](const QJsonObject &body) {
        const auto rows = body.value(QStringLiteral("data")).toObject().value(QStringLiteral("lists"));
        if (integer(body.value(QStringLiteral("error_code"))) != 0 ||
            integer(body.value(QStringLiteral("status"))) != 1 || !rows.isArray()) {
            emit failed(token, QStringLiteral("invalid_response"));
            return;
        }
        QVector<LyricsCandidate> songs;
        QSet<QString> seen;
        static const QRegularExpression validHash(QStringLiteral("^[a-fA-F0-9]{32}$"));
        for (const auto &rowValue : rows.toArray()) {
            const auto row = rowValue.toObject();
            auto variants = row.value(QStringLiteral("Grp")).toArray();
            variants.prepend(row);
            for (const auto &variantValue : variants) {
                const auto item = variantValue.toObject();
                const auto hash = item.value(QStringLiteral("FileHash"))
                                      .toString(item.value(QStringLiteral("hash")).toString());
                if (!validHash.match(hash).hasMatch() || seen.contains(hash.toCaseFolded()))
                    continue;
                seen.insert(hash.toCaseFolded());
                LyricsCandidate song;
                song.songResult = true;
                // Retain song-resolution details in the provider; the client selects a candidate index.
                song.resolverHandle = QString::number(++m_handle);
                m_resolutions.insert(song.resolverHandle,
                                     {hash, {}, integer(item.value(QStringLiteral("MixSongID")))});
                song.document.source = QStringLiteral("kugou");
                const auto itemImage = item.value(QStringLiteral("Image")).toString();
                song.document.coverUrl =
                    coverUrl(itemImage.isEmpty() ? row.value(QStringLiteral("Image")).toString() : itemImage);
                song.document.matched.title =
                    cleanText(item.value(QStringLiteral("SongName"))
                                  .toString(row.value(QStringLiteral("SongName")).toString()));
                song.document.matched.artist =
                    cleanText(item.value(QStringLiteral("SingerName"))
                                  .toString(row.value(QStringLiteral("SingerName")).toString()));
                song.document.matched.album =
                    cleanText(item.value(QStringLiteral("AlbumName"))
                                  .toString(row.value(QStringLiteral("AlbumName")).toString()));
                song.document.matched.durationMs = integer(item.value(QStringLiteral("Duration"))) * 1000;
                songs.append(song);
                if (songs.size() >= 100)
                    break;
            }
            if (songs.size() >= 100)
                break;
        }
        if (songs.isEmpty() && !rows.toArray().isEmpty())
            emit failed(token, QStringLiteral("invalid_response"));
        else
            emit completed(token, songs);
    });
}

void KugouProvider::choose(const LyricsCandidate &candidate, quint64 token) {
    const auto resolution = m_resolutions.value(candidate.resolverHandle);
    if (candidate.songResult) {
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("keywords"), candidate.document.matched.title);
        params.addQueryItem(QStringLiteral("hash"), resolution.hash);
        params.addQueryItem(QStringLiteral("man"), QStringLiteral("yes"));
        if (candidate.document.matched.durationMs > 0)
            params.addQueryItem(QStringLiteral("duration"),
                                QString::number(candidate.document.matched.durationMs));
        if (resolution.audioId > 0)
            params.addQueryItem(QStringLiteral("album_audio_id"), QString::number(resolution.audioId));
        get(QStringLiteral("/search/lyric"), params, token,
            [this, token, candidate](const QJsonObject &body) {
                const auto rows = body.value(QStringLiteral("candidates"));
                if (integer(body.value(QStringLiteral("status"))) == 404 && rows.isArray() &&
                    rows.toArray().isEmpty()) {
                    emit completed(token, {});
                    return;
                }
                if (integer(body.value(QStringLiteral("status"))) != 200 || !rows.isArray()) {
                    emit failed(token, QStringLiteral("invalid_response"));
                    return;
                }
                QVector<LyricsCandidate> lyrics;
                for (const auto &value : rows.toArray()) {
                    const auto row = value.toObject();
                    const auto id = integer(row.value(QStringLiteral("id")));
                    const auto key = row.value(QStringLiteral("accesskey")).toString();
                    if (id <= 0 || key.isEmpty())
                        continue;
                    LyricsCandidate item;
                    item.document.source = QStringLiteral("kugou");
                    item.document.providerId = id;
                    item.document.matched = candidate.document.matched;
                    item.document.coverUrl = candidate.document.coverUrl;
                    item.document.matched.title =
                        row.value(QStringLiteral("song")).toString(item.document.matched.title);
                    item.document.matched.artist =
                        row.value(QStringLiteral("singer")).toString(item.document.matched.artist);
                    const auto duration = integer(row.value(QStringLiteral("duration")));
                    if (duration > 0)
                        item.document.matched.durationMs = duration;
                    item.resolverHandle = QString::number(++m_handle);
                    m_resolutions.insert(item.resolverHandle, {{}, key, 0});
                    lyrics.append(item);
                    if (lyrics.size() >= 100)
                        break;
                }
                if (lyrics.isEmpty() && !rows.toArray().isEmpty())
                    emit failed(token, QStringLiteral("invalid_response"));
                else
                    emit completed(token, lyrics);
            });
        return;
    }
    // KRC failure falls back to LRC for this selected version, rather than silently choosing another song.
    const auto downloadLrc = [this, token, candidate, resolution]() {
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("id"), QString::number(candidate.document.providerId));
        params.addQueryItem(QStringLiteral("accesskey"), resolution.accessKey);
        params.addQueryItem(QStringLiteral("fmt"), QStringLiteral("lrc"));
        params.addQueryItem(QStringLiteral("decode"), QStringLiteral("1"));
        get(QStringLiteral("/lyric"), params, token, [this, token, candidate](const QJsonObject &body) {
            auto document = candidate.document;
            const auto lyrics = body.value(QStringLiteral("decodeContent")).toString();
            if (LrcParser::parse(lyrics).isEmpty())
                document.plainLyrics = lyrics;
            else
                document.syncedLyrics = lyrics;
            document.validate();
            if (document.isEmpty())
                emit failed(token, QStringLiteral("invalid_response"));
            else
                emit resolved(token, document);
        });
    };
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("ver"), QStringLiteral("1"));
    params.addQueryItem(QStringLiteral("client"), QStringLiteral("pc"));
    params.addQueryItem(QStringLiteral("id"), QString::number(candidate.document.providerId));
    params.addQueryItem(QStringLiteral("accesskey"), resolution.accessKey);
    params.addQueryItem(QStringLiteral("fmt"), QStringLiteral("krc"));
    params.addQueryItem(QStringLiteral("charset"), QStringLiteral("utf8"));
    // The Worker lyric endpoint supports LRC; fetch KRC directly to preserve word timing.
    get(
        QStringLiteral("https://lyrics.kugou.com/download"), params, token,
        [this, token, candidate, downloadLrc](const QJsonObject &body) {
            const auto payload = readKrcResponse(body);
            if (!payload) {
                downloadLrc();
                return;
            }
            auto document = candidate.document;
            document.krcLyrics = payload->text;
            emit resolved(token, document);
        },
        [downloadLrc](const QString &) { downloadLrc(); });
}

} // namespace nekotune
