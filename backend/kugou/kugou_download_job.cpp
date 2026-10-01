#include "kugou/kugou_download_job.h"
#include "kugou/krc_response.h"

#include "domain/krc_parser.h"
#include "domain/lrc_parser.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

#include <limits>

namespace nekotune {
namespace {
constexpr qint64 kMaxAudioBytes = 100LL * 1024 * 1024;
constexpr qint64 kMaxJsonBytes = 4LL * 1024 * 1024;
constexpr qint64 kMaxCoverBytes = 5LL * 1024 * 1024;
QString jsonString(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toInteger());
    return {};
}

QString contentSuffix(const QString &header) {
    const auto type = header.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    if (type == QStringLiteral("audio/mpeg") || type == QStringLiteral("audio/mp3"))
        return QStringLiteral(".mp3");
    if (type == QStringLiteral("audio/flac"))
        return QStringLiteral(".flac");
    if (type == QStringLiteral("audio/aac"))
        return QStringLiteral(".aac");
    if (type == QStringLiteral("audio/mp4") || type == QStringLiteral("audio/x-m4a"))
        return QStringLiteral(".m4a");
    if (type == QStringLiteral("audio/ogg"))
        return QStringLiteral(".ogg");
    return {};
}
} // namespace

void KugouDownloadJob::discardAudio() {
    if (m_audioFile) {
        m_audioFile->cancelWriting();
        m_audioFile.reset();
    }
}

void KugouDownloadJob::beginAudio(const QUrl &url, int redirects) {
    if (!trustedAudioUrl(url) || redirects > 3) {
        finishOperation(KugouEventType::OperationFailed, QStringLiteral("Unsafe audio redirect"));
        return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(60000);
    request.setRawHeader("User-Agent", "Mozilla/5.0 NekoTune/1.0");
    auto *reply = m_manager->get(request);
    m_reply = reply;
    m_audioBytes = 0;
    m_audioError.clear();
    connect(reply, &QIODevice::readyRead, this, [this, reply]() {
        if (!m_audioError.isEmpty()) {
            reply->readAll();
            return;
        }
        const auto chunk = reply->readAll();
        if (chunk.isEmpty())
            return;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http != 200)
            return;
        const auto contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (contentLength > kMaxAudioBytes) {
            m_audioError = QStringLiteral("Audio exceeds 100 MiB");
            reply->abort();
            return;
        }
        if (!m_audioFile) {
            const auto suffix = contentSuffix(reply->header(QNetworkRequest::ContentTypeHeader).toString());
            if (suffix.isEmpty()) {
                m_audioError = QStringLiteral("Response is not a supported audio type");
                reply->abort();
                return;
            }
            if (!QDir().mkpath(m_musicDirectory)) {
                m_audioError = QStringLiteral("Cannot create music directory");
                reply->abort();
                return;
            }
            const auto base = safeName(m_selected.title) + QStringLiteral(" - ") +
                              safeName(m_selected.artist) + QStringLiteral(" [") + m_selected.hash.toLower() +
                              QStringLiteral("]");
            m_audioPath = QDir(m_musicDirectory).filePath(base + suffix);
            if (QFileInfo::exists(m_audioPath)) {
                m_audioError = QStringLiteral("Audio file already exists");
                reply->abort();
                return;
            }
            m_audioFile = std::make_unique<QSaveFile>(m_audioPath);
            m_audioFile->setDirectWriteFallback(false);
            if (!m_audioFile->open(QIODevice::WriteOnly)) {
                m_audioError = QStringLiteral("Cannot create audio file");
                reply->abort();
                return;
            }
        }
        m_audioBytes += chunk.size();
        if (m_audioBytes > kMaxAudioBytes || m_audioFile->write(chunk) != chunk.size()) {
            m_audioError = m_audioBytes > kMaxAudioBytes ? QStringLiteral("Audio exceeds 100 MiB")
                                                         : QStringLiteral("Cannot write audio file");
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (m_downloadActive)
            emit eventReady({.type = KugouEventType::DownloadProgress, .received = received, .total = total});
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, redirects]() {
        if (m_reply == reply)
            m_reply = nullptr;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const QUrl next = reply->url().resolved(redirect);
        const auto networkError = reply->error();
        reply->deleteLater();
        if (m_cancelled) {
            discardAudio();
            finishOperation(KugouEventType::DownloadCancelled);
            return;
        }
        if (http >= 300 && http < 400) {
            discardAudio();
            if (redirect.isEmpty() || !trustedAudioUrl(next)) {
                finishOperation(KugouEventType::OperationFailed, QStringLiteral("Unsafe audio redirect"));
            } else
                beginAudio(next, redirects + 1);
            return;
        }
        if (http != 200 || networkError != QNetworkReply::NoError || !m_audioError.isEmpty() ||
            !m_audioFile || m_audioBytes == 0 || QFileInfo::exists(m_audioPath)) {
            const auto message =
                !m_audioError.isEmpty() ? m_audioError : QStringLiteral("Audio download failed");
            discardAudio();
            finishOperation(KugouEventType::OperationFailed, message);
            return;
        }
        if (!m_audioFile->commit()) {
            discardAudio();
            finishOperation(KugouEventType::OperationFailed, QStringLiteral("Cannot save audio file"));
            return;
        }
        m_audioFile.reset();
        fetchLyrics();
    });
}

void KugouDownloadJob::fetchLyrics() {
    if (m_cancelled) {
        finishAudio(QStringLiteral("skipped"));
        return;
    }
    emit eventReady({.type = KugouEventType::DownloadStage, .stage = QStringLiteral("lyrics")});
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hash"), m_selected.hash);
    query.addQueryItem(QStringLiteral("keywords"), m_selected.title);
    query.addQueryItem(QStringLiteral("man"), QStringLiteral("yes"));
    if (m_selected.durationMs > 0)
        query.addQueryItem(QStringLiteral("duration"), QString::number(m_selected.durationMs));
    if (!m_selected.audioId.isEmpty())
        query.addQueryItem(QStringLiteral("album_audio_id"), m_selected.audioId);
    requestJson(QStringLiteral("/search/lyric?") + query.toString(QUrl::FullyEncoded), {},
                [this](const QJsonObject &result, const QString &error) {
                    if (m_cancelled) {
                        finishAudio(QStringLiteral("skipped"));
                        return;
                    }
                    const auto candidates = result.value(QStringLiteral("candidates")).toArray();
                    if (!error.isEmpty() || !result.value(QStringLiteral("candidates")).isArray()) {
                        finishAudio(QStringLiteral("error"));
                        return;
                    }
                    if (candidates.isEmpty()) {
                        finishAudio(QStringLiteral("none"));
                        return;
                    }
                    int selectedIndex = -1;
                    qint64 bestDifference = std::numeric_limits<qint64>::max();
                    bool ambiguous = false;
                    for (int index = 0; index < candidates.size(); ++index) {
                        const auto item = candidates.at(index).toObject();
                        if (normalizedName(item.value(QStringLiteral("song")).toString()) !=
                                normalizedName(m_selected.title) ||
                            normalizedName(item.value(QStringLiteral("singer")).toString()) !=
                                normalizedName(m_selected.artist))
                            continue;
                        const auto candidateDuration =
                            item.value(QStringLiteral("duration")).toVariant().toLongLong();
                        const bool comparable = candidateDuration > 0 && m_selected.durationMs > 0;
                        const auto difference =
                            comparable ? qAbs(candidateDuration - m_selected.durationMs) : 0;
                        if (comparable && difference > 3000)
                            continue;
                        if (difference < bestDifference) {
                            bestDifference = difference;
                            selectedIndex = index;
                            ambiguous = false;
                        } else if (difference == bestDifference)
                            ambiguous = true;
                    }
                    if (selectedIndex < 0 || ambiguous) {
                        finishAudio(QStringLiteral("uncertain"));
                        return;
                    }
                    const auto item = candidates.at(selectedIndex).toObject();
                    fetchKrc(item, [this, item](const QString &krcStatus) { fetchLrc(item, krcStatus); });
                });
}

void KugouDownloadJob::fetchKrc(const QJsonObject &candidate, std::function<void(const QString &)> callback) {
    const auto path = QFileInfo(m_audioPath).absolutePath() + QLatin1Char('/') +
                      QFileInfo(m_audioPath).completeBaseName() + QStringLiteral(".krc");
    if (QFileInfo::exists(path)) {
        callback(QStringLiteral("existing"));
        return;
    }
    QUrl url(QStringLiteral("https://lyrics.kugou.com/download"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ver"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("client"), QStringLiteral("pc"));
    query.addQueryItem(QStringLiteral("id"), jsonString(candidate.value(QStringLiteral("id"))));
    query.addQueryItem(QStringLiteral("accesskey"), candidate.value(QStringLiteral("accesskey")).toString());
    query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("krc"));
    query.addQueryItem(QStringLiteral("charset"), QStringLiteral("utf8"));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "NekoTune/0.1.0 (desktop music player)");
    auto *reply = m_manager->get(request);
    m_reply = reply;
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes]() {
        bytes->append(reply->readAll());
        if (bytes->size() > kMaxJsonBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bytes, path, callback = std::move(callback)]() {
                if (m_reply == reply)
                    m_reply = nullptr;
                bytes->append(reply->readAll());
                const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const auto error = reply->error();
                reply->deleteLater();
                if (m_cancelled) {
                    callback(QStringLiteral("skipped"));
                    return;
                }
                if (http != 200 || error != QNetworkReply::NoError || bytes->size() > kMaxJsonBytes) {
                    callback(QStringLiteral("error"));
                    return;
                }
                const auto json = QJsonDocument::fromJson(*bytes);
                const auto payload = json.isObject() ? readKrcResponse(json.object()) : std::nullopt;
                if (!payload) {
                    callback(QStringLiteral("error"));
                    return;
                }
                if (QFileInfo::exists(path)) {
                    callback(QStringLiteral("existing"));
                    return;
                }
                QSaveFile file(path);
                file.setDirectWriteFallback(false);
                if (!file.open(QIODevice::WriteOnly) ||
                    file.write(payload->binary) != payload->binary.size() || !file.commit()) {
                    callback(QStringLiteral("error"));
                    return;
                }
                callback(QStringLiteral("saved"));
            });
}

void KugouDownloadJob::fetchLrc(const QJsonObject &candidate, const QString &krcStatus) {
    if (m_cancelled) {
        finishAudio(QStringLiteral("skipped"));
        return;
    }
    QUrlQuery lyricQuery;
    lyricQuery.addQueryItem(QStringLiteral("id"), jsonString(candidate.value(QStringLiteral("id"))));
    lyricQuery.addQueryItem(QStringLiteral("accesskey"),
                            candidate.value(QStringLiteral("accesskey")).toString());
    lyricQuery.addQueryItem(QStringLiteral("fmt"), QStringLiteral("lrc"));
    lyricQuery.addQueryItem(QStringLiteral("decode"), QStringLiteral("1"));
    requestJson(
        QStringLiteral("/lyric?") + lyricQuery.toString(QUrl::FullyEncoded), {},
        [this, krcStatus](const QJsonObject &lyricResult, const QString &lyricError) {
            if (m_cancelled) {
                finishAudio(QStringLiteral("skipped"));
                return;
            }
            QString lrcStatus = QStringLiteral("error");
            const auto lyric = lyricResult.value(QStringLiteral("decodeContent")).toString();
            if (lyricError.isEmpty() && !LrcParser::parse(lyric).isEmpty()) {
                const auto path = QFileInfo(m_audioPath).absolutePath() + QLatin1Char('/') +
                                  QFileInfo(m_audioPath).completeBaseName() + QStringLiteral(".lrc");
                if (QFileInfo::exists(path))
                    lrcStatus = QStringLiteral("existing");
                else {
                    QSaveFile file(path);
                    file.setDirectWriteFallback(false);
                    const auto bytes = lyric.toUtf8();
                    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
                        lrcStatus = QStringLiteral("saved");
                }
            }
            finishAudio(krcStatus == QStringLiteral("saved") || krcStatus == QStringLiteral("existing")
                            ? krcStatus
                            : lrcStatus);
        });
}

void KugouDownloadJob::finishAudio(const QString &lyricStatus) {
    if (m_cancelled) {
        completeAudio(lyricStatus, QStringLiteral("skipped"));
        return;
    }
    const QFileInfo audio(m_audioPath);
    const auto base = QDir(audio.absolutePath()).filePath(audio.completeBaseName());
    for (const auto &suffix :
         {QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".png"), QStringLiteral(".webp")}) {
        if (QFileInfo(base + suffix).isFile()) {
            completeAudio(lyricStatus, QStringLiteral("existing"));
            return;
        }
    }
    if (m_selected.coverUrl.isEmpty()) {
        completeAudio(lyricStatus, QStringLiteral("none"));
        return;
    }
    fetchCover(lyricStatus, m_selected.coverUrl);
}

void KugouDownloadJob::fetchCover(const QString &lyricStatus, const QUrl &url, int redirects) {
    if (!trustedCoverRedirect(url) || redirects > 2) {
        completeAudio(lyricStatus, QStringLiteral("error"));
        return;
    }
    if (redirects == 0)
        emit eventReady({.type = KugouEventType::DownloadStage, .stage = QStringLiteral("cover")});
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setRawHeader("Accept", "image/jpeg,image/png,image/webp");
    request.setRawHeader("User-Agent", "Mozilla/5.0 NekoTune/1.0");
    auto *reply = m_manager->get(request);
    m_reply = reply;
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes]() {
        if (reply->bytesAvailable() > kMaxCoverBytes - bytes->size()) {
            reply->setProperty("cover_oversize", true);
            reply->abort();
            return;
        }
        bytes->append(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, lyricStatus, redirects]() {
        if (m_reply == reply)
            m_reply = nullptr;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const QUrl next = reply->url().resolved(redirect);
        const bool oversize = reply->property("cover_oversize").toBool() ||
                              reply->bytesAvailable() > kMaxCoverBytes - bytes->size();
        if (!oversize)
            bytes->append(reply->readAll());
        const auto networkError = reply->error();
        reply->deleteLater();
        if (m_cancelled) {
            completeAudio(lyricStatus, QStringLiteral("skipped"));
            return;
        }
        if (http >= 300 && http < 400) {
            if (redirect.isEmpty() || !trustedCoverRedirect(next))
                completeAudio(lyricStatus, QStringLiteral("error"));
            else
                fetchCover(lyricStatus, next, redirects + 1);
            return;
        }
        if (http != 200 || networkError != QNetworkReply::NoError || oversize || bytes->isEmpty()) {
            completeAudio(lyricStatus, QStringLiteral("error"));
            return;
        }
        QBuffer buffer(bytes.get());
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const auto format = reader.format().toLower();
        const auto dimensions = reader.size();
        if ((format != "jpeg" && format != "jpg" && format != "png" && format != "webp") ||
            !dimensions.isValid() || dimensions.width() > 4096 || dimensions.height() > 4096 ||
            reader.read().isNull()) {
            completeAudio(lyricStatus, QStringLiteral("error"));
            return;
        }
        const auto suffix = format == "jpeg" || format == "jpg" ? QStringLiteral(".jpg")
                            : format == "png"                   ? QStringLiteral(".png")
                                                                : QStringLiteral(".webp");
        const QFileInfo audio(m_audioPath);
        const auto path = QDir(audio.absolutePath()).filePath(audio.completeBaseName() + suffix);
        if (QFileInfo::exists(path)) {
            completeAudio(lyricStatus, QStringLiteral("existing"));
            return;
        }
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(*bytes) != bytes->size() || !file.commit()) {
            completeAudio(lyricStatus, QStringLiteral("error"));
            return;
        }
        completeAudio(lyricStatus, QStringLiteral("saved"));
    });
}

void KugouDownloadJob::completeAudio(const QString &lyricStatus, const QString &coverStatus) {
    m_downloadActive = false;
    emit audioReady(m_audioPath, lyricStatus, coverStatus, m_selected.title, m_selected.artist);
}

QString KugouDownloadJob::cancelDownload() {
    if (!m_downloadActive)
        return QStringLiteral("No active Kugou download");
    m_cancelled = true;
    m_api.cancel();
    if (m_reply)
        m_reply->abort();
    else if (!m_audioPath.isEmpty())
        finishAudio(QStringLiteral("skipped"));
    else {
        discardAudio();
        finishOperation(KugouEventType::DownloadCancelled);
    }
    return {};
}

QString KugouDownloadJob::safeName(const QString &value) {
    QString result = value;
    result.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), QStringLiteral("_"));
    result = result.trimmed();
    while (result.endsWith(QLatin1Char('.')))
        result.chop(1);
    QString limited;
    qsizetype bytes = 0;
    for (const auto codepoint : result.toUcs4()) {
        const char32_t scalar = static_cast<char32_t>(codepoint);
        const auto character = QString::fromUcs4(&scalar, 1);
        const auto width = character.toUtf8().size();
        if (bytes + width > 64)
            break;
        limited += character;
        bytes += width;
    }
    return limited.isEmpty() ? QStringLiteral("Unknown") : limited;
}

QString KugouDownloadJob::normalizedName(const QString &value) {
    QString result;
    for (const auto ch : value.normalized(QString::NormalizationForm_KC).toCaseFolded())
        if (ch.isLetterOrNumber())
            result.append(ch);
    return result;
}

bool KugouDownloadJob::trustedAudioUrl(const QUrl &url) {
    const auto host = url.host().toLower();
    return url.scheme() == QStringLiteral("https") && host.endsWith(QStringLiteral(".kugou.com")) &&
           url.userInfo().isEmpty() && (url.port(-1) == -1 || url.port() == 443);
}

QUrl KugouDownloadJob::trustedCoverUrl(const QString &image) {
    QUrl url(image.trimmed().replace(QStringLiteral("{size}"), QStringLiteral("240")));
    if ((url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) ||
        url.host().compare(QStringLiteral("imge.kugou.com"), Qt::CaseInsensitive) != 0 ||
        !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port() != 80 && url.port() != 443))
        return {};
    url.setScheme(QStringLiteral("https"));
    url.setPort(-1);
    url.setFragment({});
    return url;
}

bool KugouDownloadJob::trustedCoverRedirect(const QUrl &url) {
    return url.scheme() == QStringLiteral("https") &&
           url.host().compare(QStringLiteral("imge.kugou.com"), Qt::CaseInsensitive) == 0 &&
           url.userInfo().isEmpty() && (url.port(-1) == -1 || url.port() == 443);
}
bool KugouDownloadJob::reuseExisting() {
    const auto base = safeName(m_selected.title) + QStringLiteral(" - ") + safeName(m_selected.artist) +
                      QStringLiteral(" [") + m_selected.hash.toLower() + QStringLiteral("]");
    for (const auto &suffix : {QStringLiteral(".mp3"), QStringLiteral(".flac"), QStringLiteral(".aac"),
                               QStringLiteral(".m4a"), QStringLiteral(".ogg")}) {
        const auto existing = QDir(m_musicDirectory).filePath(base + suffix);
        if (QFileInfo(existing).isFile() && QFileInfo(existing).size() > 0) {
            m_audioPath = existing;
            fetchLyrics();
            return true;
        }
    }
    return false;
}

void KugouDownloadJob::finishOperation(KugouEventType event, const QString &message) {
    m_downloadActive = false;
    emit eventReady({.type = event, .message = message});
}
void KugouDownloadJob::shutdown() {
    m_cancelled = true;
    m_api.cancel();
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    discardAudio();
    m_downloadActive = false;
}
} // namespace nekotune
