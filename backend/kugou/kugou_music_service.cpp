#include "kugou/kugou_music_service.h"

#include "lyrics/lrc_parser.h"
#include "lyrics/krc_parser.h"

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
namespace {
constexpr qint64 kMaxAudioBytes = 100LL * 1024 * 1024;
constexpr qint64 kMaxJsonBytes = 4LL * 1024 * 1024;
constexpr qint64 kMaxCoverBytes = 5LL * 1024 * 1024;
const QRegularExpression kHashPattern(QStringLiteral("^[a-fA-F0-9]{32}$"));
const QRegularExpression kMobilePattern(QStringLiteral("^1[3-9][0-9]{9}$"));
const QRegularExpression kCodePattern(QStringLiteral("^[0-9]{4,8}$"));

QString jsonString(const QJsonValue &value)
{
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toInteger());
    return {};
}

bool validDfid(const QString &value)
{
    return !value.isEmpty() && value != QStringLiteral("0") && value != QStringLiteral("-")
        && value != QStringLiteral("undefined");
}

QString contentSuffix(const QString &header)
{
    const auto type = header.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    if (type == QStringLiteral("audio/mpeg") || type == QStringLiteral("audio/mp3")) return QStringLiteral(".mp3");
    if (type == QStringLiteral("audio/flac")) return QStringLiteral(".flac");
    if (type == QStringLiteral("audio/aac")) return QStringLiteral(".aac");
    if (type == QStringLiteral("audio/mp4") || type == QStringLiteral("audio/x-m4a")) return QStringLiteral(".m4a");
    if (type == QStringLiteral("audio/ogg")) return QStringLiteral(".ogg");
    return {};
}
} // namespace

KugouMusicService::KugouMusicService(QObject *parent, QNetworkAccessManager *manager,
                                     const QUrl &baseUrl, const QString &sessionPath,
                                     const QString &musicDirectory, const QString &keyPath)
    : QObject(parent), m_manager(manager ? manager : new QNetworkAccessManager(this))
{
    const auto env = QProcessEnvironment::systemEnvironment();
    m_baseUrl = baseUrl.isEmpty()
        ? QUrl(env.value(QStringLiteral("KUGOU_MUSIC_API_URL"),
                         QStringLiteral("https://kugou-lyrics-api.lyuy.workers.dev"))) : baseUrl;
    const auto host = m_baseUrl.host().toLower();
    const bool loopback = host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1")
                          || host == QStringLiteral("::1");
    if (m_baseUrl.host().isEmpty() || !m_baseUrl.userInfo().isEmpty() || !m_baseUrl.query().isEmpty()
        || !m_baseUrl.fragment().isEmpty() || (m_baseUrl.path() != QString() && m_baseUrl.path() != QStringLiteral("/"))
        || !(m_baseUrl.scheme() == QStringLiteral("https") || (loopback && m_baseUrl.scheme() == QStringLiteral("http")))) {
        m_configurationError = QStringLiteral("Invalid Kugou Worker URL");
    }
    m_baseUrl.setPath({});
    m_keyPath = keyPath.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
              .filePath(QStringLiteral("NekoTune/kugou-account-key")) : keyPath;
    loadKey();
    m_sessionPath = sessionPath.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
              .filePath(QStringLiteral("NekoTune/kugou-session.json")) : sessionPath;
    const auto musicRoot = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    m_musicDirectory = musicDirectory.isEmpty()
        ? QDir(musicRoot.isEmpty() ? QDir::homePath() : musicRoot).filePath(QStringLiteral("NekoTune"))
        : musicDirectory;
    loadSession();
}

QJsonObject KugouMusicService::status() const
{
    return {{QStringLiteral("configured"), m_configurationError.isEmpty() && !m_key.isEmpty()},
            {QStringLiteral("key_saved"), m_keySaved},
            {QStringLiteral("logged_in"), !m_cookies.value(QStringLiteral("token")).isEmpty()
                && !m_cookies.value(QStringLiteral("userid")).isEmpty()
                && validDfid(m_cookies.value(QStringLiteral("dfid")))},
            {QStringLiteral("busy"), m_busy},
            {QStringLiteral("download_active"), m_downloadActive}};
}

void KugouMusicService::loadKey()
{
    m_key.clear();
    m_keySaved = false;
    const QFileInfo stored(m_keyPath);
    if (stored.exists() && !stored.isSymLink()
        && QFile::setPermissions(m_keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        QFile file(m_keyPath);
        if (file.open(QIODevice::ReadOnly) && file.size() <= 4096) {
            const auto candidate = QString::fromUtf8(file.readAll()).trimmed();
            if (!candidate.isEmpty() && candidate.size() <= 4096
                && !candidate.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")))) {
                m_key = candidate;
                m_keySaved = true;
                return;
            }
        }
    }
    const auto env = QProcessEnvironment::systemEnvironment();
    m_key = env.value(QStringLiteral("KUGOU_ACCOUNT_API_KEY")).trimmed();
    if (!m_key.isEmpty()) return;
    const auto filePath = env.value(QStringLiteral("KUGOU_ACCOUNT_API_KEY_FILE"));
    QFile file(filePath);
    if (!filePath.isEmpty() && file.open(QIODevice::ReadOnly) && file.size() <= 4096)
        m_key = QString::fromUtf8(file.readAll()).trimmed();
}

QString KugouMusicService::saveAccountKey(const QString &key)
{
    if (m_busy) return QStringLiteral("Kugou operation already in progress");
    const auto trimmed = key.trimmed();
    const auto bytes = trimmed.toUtf8();
    if (trimmed.isEmpty() || bytes.size() > 4096
        || trimmed.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]"))))
        return QStringLiteral("Kugou account key must be 1 to 4096 printable bytes");
    if (QFileInfo(m_keyPath).isSymLink() || !QDir().mkpath(QFileInfo(m_keyPath).absolutePath()))
        return QStringLiteral("Cannot prepare private Kugou key file");
    QSaveFile file(m_keyPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return QStringLiteral("Cannot write private Kugou key file");
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file.cancelWriting();
        return QStringLiteral("Cannot secure private Kugou key file");
    }
    if (file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Cannot save private Kugou key file");
    if (!QFile::setPermissions(m_keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        QFile::remove(m_keyPath);
        return QStringLiteral("Cannot secure private Kugou key file");
    }
    m_key = trimmed;
    m_keySaved = true;
    return {};
}

QString KugouMusicService::clearAccountKey()
{
    if (m_busy) return QStringLiteral("Kugou operation already in progress");
    const QFileInfo stored(m_keyPath);
    if (stored.isSymLink() || (stored.exists() && !QFile::remove(m_keyPath)))
        return QStringLiteral("Cannot remove private Kugou key file");
    loadKey();
    return {};
}

void KugouMusicService::loadSession()
{
    const QFileInfo info(m_sessionPath);
    if (info.isSymLink() || (info.exists() && !QFile::setPermissions(
            m_sessionPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner))) return;
    QFile file(m_sessionPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024) return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value(QStringLiteral("version")).toInt() != 1) return;
    const auto cookies = object.value(QStringLiteral("cookies")).toObject();
    for (auto it = cookies.begin(); it != cookies.end(); ++it)
        if (it.value().isString()) m_cookies.insert(it.key(), it.value().toString());
}

bool KugouMusicService::saveSession()
{
    if (QFileInfo(m_sessionPath).isSymLink()) return false;
    if (!QDir().mkpath(QFileInfo(m_sessionPath).absolutePath())) return false;
    QSaveFile file(m_sessionPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QJsonObject cookies;
    for (auto it = m_cookies.cbegin(); it != m_cookies.cend(); ++it) cookies.insert(it.key(), it.value());
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                                   {QStringLiteral("cookies"), cookies}}).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) return false;
    return QFile::setPermissions(m_sessionPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool KugouMusicService::businessOk(const QJsonObject &body, const QString &expected)
{
    const auto code = body.value(QStringLiteral("error_code"));
    const auto other = body.value(QStringLiteral("errcode"));
    return jsonString(body.value(QStringLiteral("status"))) == expected
        && (code.isUndefined() || jsonString(code) == QStringLiteral("0"))
        && (other.isUndefined() || jsonString(other) == QStringLiteral("0"));
}

void KugouMusicService::requestJson(const QString &route, const QJsonObject &body, JsonCallback callback)
{
    QNetworkRequest request(m_baseUrl.resolved(QUrl(route)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "Mozilla/5.0 NekoTune/1.0");
    QNetworkReply *reply = nullptr;
    if (body.isEmpty()) {
        reply = m_manager->get(request);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("X-Account-Key", m_key.toUtf8());
        reply = m_manager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    m_reply = reply;
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes]() {
        bytes->append(reply->readAll());
        if (bytes->size() > kMaxJsonBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, callback = std::move(callback)]() {
        if (m_reply == reply) m_reply = nullptr;
        bytes->append(reply->readAll());
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto json = QJsonDocument::fromJson(*bytes);
        const auto object = json.isObject() ? json.object() : QJsonObject{};
        QString error;
        if (m_cancelled) error = QStringLiteral("Cancelled");
        else if (bytes->size() > kMaxJsonBytes) error = QStringLiteral("Worker response too large");
        else if (http >= 300 && http < 400) error = QStringLiteral("Worker redirected unexpectedly");
        else if (http >= 400) error = object.value(QStringLiteral("error_msg")).toString(QStringLiteral("Worker HTTP %1").arg(http));
        else if (reply->error() != QNetworkReply::NoError) error = QStringLiteral("Cannot reach Kugou Worker");
        else if (!json.isObject()) error = QStringLiteral("Invalid Worker response");
        reply->deleteLater();
        callback(object, error);
    });
}

void KugouMusicService::accountRequest(const QString &route, const QJsonObject &body, JsonCallback callback)
{
    QJsonObject request = body;
    QJsonObject cookies;
    for (auto it = m_cookies.cbegin(); it != m_cookies.cend(); ++it) cookies.insert(it.key(), it.value());
    request.insert(QStringLiteral("cookies"), cookies);
    requestJson(route, request, [this, callback = std::move(callback)](QJsonObject result, const QString &error) {
        QString failure = error;
        const auto updated = result.take(QStringLiteral("cookies"));
        if (failure.isEmpty() && updated.isObject()) {
            const auto values = updated.toObject();
            for (auto it = values.begin(); it != values.end(); ++it)
                if (it.value().isString()) m_cookies.insert(it.key(), it.value().toString());
            if (!saveSession()) failure = QStringLiteral("Cannot save Kugou session");
        }
        callback(result, failure);
    });
}

void KugouMusicService::finishOperation(const QString &event, const QString &message)
{
    m_busy = false;
    m_downloadActive = false;
    QJsonObject payload{{QStringLiteral("event"), event}};
    if (!message.isEmpty()) payload.insert(QStringLiteral("message"), message);
    emit eventReady(payload);
}

QString KugouMusicService::startCodeRequest(const QString &mobile)
{
    if (!m_configurationError.isEmpty()) return m_configurationError;
    if (m_key.isEmpty()) return QStringLiteral("Set the Kugou account key in Settings or the launch environment");
    if (m_busy) return QStringLiteral("Kugou operation already in progress");
    if (!kMobilePattern.match(mobile).hasMatch()) return QStringLiteral("Invalid mainland China mobile number");
    m_busy = true;
    m_cancelled = false;
    auto send = [this, mobile]() {
        accountRequest(QStringLiteral("/captcha/sent"), {{QStringLiteral("mobile"), mobile}},
                       [this](const QJsonObject &result, const QString &error) {
            finishOperation(error.isEmpty() && businessOk(result, QStringLiteral("1"))
                                ? QStringLiteral("kugou.code_sent") : QStringLiteral("kugou.operation_failed"),
                            error.isEmpty() && !businessOk(result, QStringLiteral("1"))
                                ? QStringLiteral("Kugou rejected SMS request") : error);
        });
    };
    const auto dfid = m_cookies.value(QStringLiteral("dfid"));
    if (validDfid(dfid)) {
        send();
    } else {
        accountRequest(QStringLiteral("/register/dev"), {{QStringLiteral("cookies"), QJsonObject{}}},
                       [this, send](const QJsonObject &result, const QString &error) {
            if (!error.isEmpty() || !businessOk(result, QStringLiteral("1"))
                || !validDfid(m_cookies.value(QStringLiteral("dfid")))) {
                finishOperation(QStringLiteral("kugou.operation_failed"),
                                error.isEmpty() ? QStringLiteral("Kugou device registration failed") : error);
                return;
            }
            send();
        });
    }
    return {};
}

QString KugouMusicService::startLogin(const QString &mobile, const QString &code)
{
    if (m_key.isEmpty() || !m_configurationError.isEmpty()) return QStringLiteral("Kugou account Worker is not configured");
    if (m_busy) return QStringLiteral("Kugou operation already in progress");
    if (!kMobilePattern.match(mobile).hasMatch() || !kCodePattern.match(code).hasMatch())
        return QStringLiteral("Invalid mobile number or verification code");
    m_busy = true;
    m_cancelled = false;
    accountRequest(QStringLiteral("/login/cellphone"), {{QStringLiteral("mobile"), mobile},
                                                       {QStringLiteral("code"), code}},
                   [this](const QJsonObject &result, const QString &error) {
        const auto data = result.value(QStringLiteral("data")).toObject();
        const bool valid = businessOk(result, QStringLiteral("1"))
            && !data.value(QStringLiteral("token")).toString().isEmpty()
            && !jsonString(data.value(QStringLiteral("userid"))).isEmpty()
            && m_cookies.value(QStringLiteral("token")) == data.value(QStringLiteral("token")).toString()
            && m_cookies.value(QStringLiteral("userid")) == jsonString(data.value(QStringLiteral("userid")));
        finishOperation(error.isEmpty() && valid ? QStringLiteral("kugou.logged_in")
                                                 : QStringLiteral("kugou.operation_failed"),
                        error.isEmpty() && !valid ? QStringLiteral("Kugou login failed or needs extra verification") : error);
    });
    return {};
}

QString KugouMusicService::startSearch(const QString &keywords, int page)
{
    if (!m_configurationError.isEmpty()) return m_configurationError;
    if (m_busy) return QStringLiteral("Kugou operation already in progress");
    if (keywords.trimmed().isEmpty() || keywords.size() > 200 || page < 1 || page > 1000)
        return QStringLiteral("Invalid search keywords or page");
    m_busy = true;
    m_cancelled = false;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("keywords"), keywords.trimmed());
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("song"));
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    requestJson(QStringLiteral("/search?") + query.toString(QUrl::FullyEncoded), {},
                [this, page](const QJsonObject &result, const QString &error) {
        const auto rows = result.value(QStringLiteral("data")).toObject().value(QStringLiteral("lists")).toArray();
        if (!error.isEmpty() || !businessOk(result, QStringLiteral("1")) || !result.value(QStringLiteral("data")).toObject().value(QStringLiteral("lists")).isArray()) {
            finishOperation(QStringLiteral("kugou.operation_failed"),
                            error.isEmpty() ? QStringLiteral("Kugou search failed") : error);
            return;
        }
        m_songs.clear();
        QJsonArray songs;
        auto add = [this, &songs](const QJsonObject &item, const QJsonObject &fallback) {
            Song song;
            song.hash = item.value(QStringLiteral("FileHash")).toString(item.value(QStringLiteral("hash")).toString());
            if (!kHashPattern.match(song.hash).hasMatch() || m_songs.contains(song.hash)) return;
            song.title = item.value(QStringLiteral("SongName")).toString(fallback.value(QStringLiteral("SongName")).toString());
            song.artist = item.value(QStringLiteral("SingerName")).toString(fallback.value(QStringLiteral("SingerName")).toString());
            song.album = item.value(QStringLiteral("AlbumName")).toString(fallback.value(QStringLiteral("AlbumName")).toString());
            const auto itemImage = item.value(QStringLiteral("Image")).toString();
            song.coverUrl = trustedCoverUrl(itemImage.isEmpty()
                ? fallback.value(QStringLiteral("Image")).toString() : itemImage);
            for (auto *field : {&song.title, &song.artist, &song.album}) {
                field->remove(QRegularExpression(QStringLiteral("<[^>]*>")));
                *field = field->trimmed().left(256);
            }
            song.audioId = jsonString(item.value(QStringLiteral("MixSongID")));
            if (!QRegularExpression(QStringLiteral("^[0-9]{1,15}$")).match(song.audioId).hasMatch()) song.audioId.clear();
            song.durationMs = qMax<qint64>(0, qRound64(item.value(QStringLiteral("Duration")).toDouble() * 1000));
            m_songs.insert(song.hash, song);
            songs.append(QJsonObject{{QStringLiteral("hash"), song.hash}, {QStringLiteral("title"), song.title},
                                     {QStringLiteral("artist"), song.artist}, {QStringLiteral("album"), song.album},
                                     {QStringLiteral("duration_ms"), song.durationMs}});
        };
        for (const auto &value : rows) {
            const auto row = value.toObject();
            add(row, row);
            for (const auto &variant : row.value(QStringLiteral("Grp")).toArray()) add(variant.toObject(), row);
            if (songs.size() >= 100) break;
        }
        m_busy = false;
        emit eventReady({{QStringLiteral("event"), QStringLiteral("kugou.search_results")},
                         {QStringLiteral("songs"), songs}, {QStringLiteral("page"), page}});
    });
    return {};
}

QString KugouMusicService::safeName(const QString &value)
{
    QString result = value;
    result.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), QStringLiteral("_"));
    result = result.trimmed();
    while (result.endsWith(QLatin1Char('.'))) result.chop(1);
    QString limited;
    qsizetype bytes = 0;
    for (const auto codepoint : result.toUcs4()) {
        const char32_t scalar = static_cast<char32_t>(codepoint);
        const auto character = QString::fromUcs4(&scalar, 1);
        const auto width = character.toUtf8().size();
        if (bytes + width > 64) break;
        limited += character;
        bytes += width;
    }
    return limited.isEmpty() ? QStringLiteral("Unknown") : limited;
}

QString KugouMusicService::normalizedName(const QString &value)
{
    QString result;
    for (const auto ch : value.normalized(QString::NormalizationForm_KC).toCaseFolded())
        if (ch.isLetterOrNumber()) result.append(ch);
    return result;
}

bool KugouMusicService::trustedAudioUrl(const QUrl &url)
{
    const auto host = url.host().toLower();
    return url.scheme() == QStringLiteral("https") && host.endsWith(QStringLiteral(".kugou.com"))
        && url.userInfo().isEmpty() && (url.port(-1) == -1 || url.port() == 443);
}

QUrl KugouMusicService::trustedCoverUrl(const QString &image)
{
    QUrl url(image.trimmed().replace(QStringLiteral("{size}"), QStringLiteral("240")));
    if ((url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
        || url.host().compare(QStringLiteral("imge.kugou.com"), Qt::CaseInsensitive) != 0
        || !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port() != 80 && url.port() != 443))
        return {};
    url.setScheme(QStringLiteral("https"));
    url.setPort(-1);
    url.setFragment({});
    return url;
}

bool KugouMusicService::trustedCoverRedirect(const QUrl &url)
{
    return url.scheme() == QStringLiteral("https")
        && url.host().compare(QStringLiteral("imge.kugou.com"), Qt::CaseInsensitive) == 0
        && url.userInfo().isEmpty() && (url.port(-1) == -1 || url.port() == 443);
}

QString KugouMusicService::startDownload(const QString &hash)
{
    if (!m_configurationError.isEmpty() || m_key.isEmpty()) return QStringLiteral("Kugou account Worker is not configured");
    if (m_busy) return QStringLiteral("Kugou operation already in progress");
    if (!status().value(QStringLiteral("logged_in")).toBool()) return QStringLiteral("Log in to Kugou first");
    if (!m_songs.contains(hash)) return QStringLiteral("Search for the song again before downloading");
    m_selected = m_songs.value(hash);
    m_busy = true;
    m_downloadActive = true;
    m_cancelled = false;
    m_audioPath.clear();
    m_audioError.clear();
    const auto base = safeName(m_selected.title) + QStringLiteral(" - ") + safeName(m_selected.artist)
        + QStringLiteral(" [") + hash.toLower() + QStringLiteral("]");
    for (const auto &suffix : {QStringLiteral(".mp3"), QStringLiteral(".flac"), QStringLiteral(".aac"),
                               QStringLiteral(".m4a"), QStringLiteral(".ogg")}) {
        const auto existing = QDir(m_musicDirectory).filePath(base + suffix);
        if (QFileInfo(existing).isFile() && QFileInfo(existing).size() > 0) {
            m_audioPath = existing;
            fetchLyrics();
            return {};
        }
    }
    QJsonObject body{{QStringLiteral("hash"), hash}, {QStringLiteral("quality"), QStringLiteral("128")}};
    if (!m_selected.audioId.isEmpty()) body.insert(QStringLiteral("album_audio_id"), m_selected.audioId);
    accountRequest(QStringLiteral("/song/url"), body, [this](const QJsonObject &result, const QString &error) {
        if (m_cancelled) { finishOperation(QStringLiteral("kugou.download_cancelled")); return; }
        const auto urls = result.value(QStringLiteral("url"));
        const auto nested = result.value(QStringLiteral("data")).toObject().value(QStringLiteral("url"));
        const auto candidate = urls.isArray() ? (urls.toArray().isEmpty() ? QString() : urls.toArray().at(0).toString())
            : urls.isString() ? urls.toString()
            : nested.isArray() ? (nested.toArray().isEmpty() ? QString() : nested.toArray().at(0).toString())
            : nested.toString();
        const QUrl url(candidate);
        if (!error.isEmpty() || !(businessOk(result, QStringLiteral("1"))
                                 || businessOk(result, QStringLiteral("200"))) || !trustedAudioUrl(url)) {
            const auto code = jsonString(result.value(QStringLiteral("error_code")));
            const auto failure = result.value(QStringLiteral("fail_process")).toArray();
            const QString message = !error.isEmpty() ? error
                : code == QStringLiteral("20028") ? QStringLiteral("Kugou requires additional account verification")
                : failure.contains(QStringLiteral("pkg")) || failure.contains(QStringLiteral("buy"))
                    ? QStringLiteral("Kugou account has no playback permission for this song")
                    : QStringLiteral("Kugou did not provide a trusted audio URL");
            finishOperation(QStringLiteral("kugou.operation_failed"), message);
            return;
        }
        beginAudio(url);
    });
    return {};
}

void KugouMusicService::discardAudio()
{
    if (m_audioFile) {
        m_audioFile->cancelWriting();
        m_audioFile.reset();
    }
}

void KugouMusicService::beginAudio(const QUrl &url, int redirects)
{
    if (!trustedAudioUrl(url) || redirects > 3) {
        finishOperation(QStringLiteral("kugou.operation_failed"), QStringLiteral("Unsafe audio redirect"));
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
        if (!m_audioError.isEmpty()) { reply->readAll(); return; }
        const auto chunk = reply->readAll();
        if (chunk.isEmpty()) return;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http != 200) return;
        const auto contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (contentLength > kMaxAudioBytes) {
            m_audioError = QStringLiteral("Audio exceeds 100 MiB");
            reply->abort();
            return;
        }
        if (!m_audioFile) {
            const auto suffix = contentSuffix(reply->header(QNetworkRequest::ContentTypeHeader).toString());
            if (suffix.isEmpty()) { m_audioError = QStringLiteral("Response is not a supported audio type"); reply->abort(); return; }
            if (!QDir().mkpath(m_musicDirectory)) { m_audioError = QStringLiteral("Cannot create music directory"); reply->abort(); return; }
            const auto base = safeName(m_selected.title) + QStringLiteral(" - ") + safeName(m_selected.artist)
                + QStringLiteral(" [") + m_selected.hash.toLower() + QStringLiteral("]");
            m_audioPath = QDir(m_musicDirectory).filePath(base + suffix);
            if (QFileInfo::exists(m_audioPath)) { m_audioError = QStringLiteral("Audio file already exists"); reply->abort(); return; }
            m_audioFile = std::make_unique<QSaveFile>(m_audioPath);
            m_audioFile->setDirectWriteFallback(false);
            if (!m_audioFile->open(QIODevice::WriteOnly)) { m_audioError = QStringLiteral("Cannot create audio file"); reply->abort(); return; }
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
            emit eventReady({{QStringLiteral("event"), QStringLiteral("kugou.download_progress")},
                             {QStringLiteral("received"), received}, {QStringLiteral("total"), total}});
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, redirects]() {
        if (m_reply == reply) m_reply = nullptr;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const QUrl next = reply->url().resolved(redirect);
        const auto networkError = reply->error();
        reply->deleteLater();
        if (m_cancelled) { discardAudio(); finishOperation(QStringLiteral("kugou.download_cancelled")); return; }
        if (http >= 300 && http < 400) {
            discardAudio();
            if (redirect.isEmpty() || !trustedAudioUrl(next)) {
                finishOperation(QStringLiteral("kugou.operation_failed"), QStringLiteral("Unsafe audio redirect"));
            } else beginAudio(next, redirects + 1);
            return;
        }
        if (http != 200 || networkError != QNetworkReply::NoError || !m_audioError.isEmpty()
            || !m_audioFile || m_audioBytes == 0 || QFileInfo::exists(m_audioPath)) {
            const auto message = !m_audioError.isEmpty() ? m_audioError : QStringLiteral("Audio download failed");
            discardAudio();
            finishOperation(QStringLiteral("kugou.operation_failed"), message);
            return;
        }
        if (!m_audioFile->commit()) {
            discardAudio();
            finishOperation(QStringLiteral("kugou.operation_failed"), QStringLiteral("Cannot save audio file"));
            return;
        }
        m_audioFile.reset();
        fetchLyrics();
    });
}

void KugouMusicService::fetchLyrics()
{
    if (m_cancelled) { finishAudio(QStringLiteral("skipped")); return; }
    emit eventReady({{QStringLiteral("event"), QStringLiteral("kugou.download_stage")},
                     {QStringLiteral("stage"), QStringLiteral("lyrics")}});
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hash"), m_selected.hash);
    query.addQueryItem(QStringLiteral("keywords"), m_selected.title);
    query.addQueryItem(QStringLiteral("man"), QStringLiteral("yes"));
    if (m_selected.durationMs > 0) query.addQueryItem(QStringLiteral("duration"), QString::number(m_selected.durationMs));
    if (!m_selected.audioId.isEmpty()) query.addQueryItem(QStringLiteral("album_audio_id"), m_selected.audioId);
    requestJson(QStringLiteral("/search/lyric?") + query.toString(QUrl::FullyEncoded), {},
                [this](const QJsonObject &result, const QString &error) {
        if (m_cancelled) { finishAudio(QStringLiteral("skipped")); return; }
        const auto candidates = result.value(QStringLiteral("candidates")).toArray();
        if (!error.isEmpty() || !result.value(QStringLiteral("candidates")).isArray()) { finishAudio(QStringLiteral("error")); return; }
        if (candidates.isEmpty()) { finishAudio(QStringLiteral("none")); return; }
        int selectedIndex = -1;
        qint64 bestDifference = std::numeric_limits<qint64>::max();
        bool ambiguous = false;
        for (int index = 0; index < candidates.size(); ++index) {
            const auto item = candidates.at(index).toObject();
            if (normalizedName(item.value(QStringLiteral("song")).toString()) != normalizedName(m_selected.title)
                || normalizedName(item.value(QStringLiteral("singer")).toString()) != normalizedName(m_selected.artist)) continue;
            const auto candidateDuration = item.value(QStringLiteral("duration")).toVariant().toLongLong();
            const bool comparable = candidateDuration > 0 && m_selected.durationMs > 0;
            const auto difference = comparable ? qAbs(candidateDuration - m_selected.durationMs) : 0;
            if (comparable && difference > 3000) continue;
            if (difference < bestDifference) { bestDifference = difference; selectedIndex = index; ambiguous = false; }
            else if (difference == bestDifference) ambiguous = true;
        }
        if (selectedIndex < 0 || ambiguous) { finishAudio(QStringLiteral("uncertain")); return; }
        const auto item = candidates.at(selectedIndex).toObject();
        fetchKrc(item, [this, item](const QString &krcStatus) { fetchLrc(item, krcStatus); });
    });
}

void KugouMusicService::fetchKrc(const QJsonObject &candidate, std::function<void(const QString &)> callback)
{
    const auto path = QFileInfo(m_audioPath).absolutePath() + QLatin1Char('/')
        + QFileInfo(m_audioPath).completeBaseName() + QStringLiteral(".krc");
    if (QFileInfo::exists(path)) { callback(QStringLiteral("existing")); return; }
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
        if (bytes->size() > kMaxJsonBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, path, callback = std::move(callback)]() {
        if (m_reply == reply) m_reply = nullptr;
        bytes->append(reply->readAll());
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error();
        reply->deleteLater();
        if (m_cancelled) { callback(QStringLiteral("skipped")); return; }
        if (http != 200 || error != QNetworkReply::NoError || bytes->size() > kMaxJsonBytes) {
            callback(QStringLiteral("error")); return;
        }
        const auto json = QJsonDocument::fromJson(*bytes);
        const auto payload = json.isObject() ? KrcParser::fromApi(json.object()) : std::nullopt;
        if (!payload) { callback(QStringLiteral("error")); return; }
        if (QFileInfo::exists(path)) { callback(QStringLiteral("existing")); return; }
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(payload->binary) != payload->binary.size() || !file.commit()) {
            callback(QStringLiteral("error")); return;
        }
        callback(QStringLiteral("saved"));
    });
}

void KugouMusicService::fetchLrc(const QJsonObject &candidate, const QString &krcStatus)
{
    if (m_cancelled) { finishAudio(QStringLiteral("skipped")); return; }
    QUrlQuery lyricQuery;
    lyricQuery.addQueryItem(QStringLiteral("id"), jsonString(candidate.value(QStringLiteral("id"))));
    lyricQuery.addQueryItem(QStringLiteral("accesskey"), candidate.value(QStringLiteral("accesskey")).toString());
    lyricQuery.addQueryItem(QStringLiteral("fmt"), QStringLiteral("lrc"));
    lyricQuery.addQueryItem(QStringLiteral("decode"), QStringLiteral("1"));
    requestJson(QStringLiteral("/lyric?") + lyricQuery.toString(QUrl::FullyEncoded), {},
                [this, krcStatus](const QJsonObject &lyricResult, const QString &lyricError) {
        if (m_cancelled) { finishAudio(QStringLiteral("skipped")); return; }
        QString lrcStatus = QStringLiteral("error");
        const auto lyric = lyricResult.value(QStringLiteral("decodeContent")).toString();
        if (lyricError.isEmpty() && !LrcParser::parse(lyric).isEmpty()) {
            const auto path = QFileInfo(m_audioPath).absolutePath() + QLatin1Char('/')
                + QFileInfo(m_audioPath).completeBaseName() + QStringLiteral(".lrc");
            if (QFileInfo::exists(path)) lrcStatus = QStringLiteral("existing");
            else {
                QSaveFile file(path);
                file.setDirectWriteFallback(false);
                const auto bytes = lyric.toUtf8();
                if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
                    lrcStatus = QStringLiteral("saved");
            }
        }
        finishAudio(krcStatus == QStringLiteral("saved") || krcStatus == QStringLiteral("existing")
                        ? krcStatus : lrcStatus);
    });
}

void KugouMusicService::finishAudio(const QString &lyricStatus)
{
    if (m_cancelled) { completeAudio(lyricStatus, QStringLiteral("skipped")); return; }
    const QFileInfo audio(m_audioPath);
    const auto base = QDir(audio.absolutePath()).filePath(audio.completeBaseName());
    for (const auto &suffix : {QStringLiteral(".jpg"), QStringLiteral(".jpeg"),
                               QStringLiteral(".png"), QStringLiteral(".webp")}) {
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

void KugouMusicService::fetchCover(const QString &lyricStatus, const QUrl &url, int redirects)
{
    if (!trustedCoverRedirect(url) || redirects > 2) {
        completeAudio(lyricStatus, QStringLiteral("error"));
        return;
    }
    if (redirects == 0)
        emit eventReady({{QStringLiteral("event"), QStringLiteral("kugou.download_stage")},
                         {QStringLiteral("stage"), QStringLiteral("cover")}});
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
        if (m_reply == reply) m_reply = nullptr;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const QUrl next = reply->url().resolved(redirect);
        const bool oversize = reply->property("cover_oversize").toBool()
            || reply->bytesAvailable() > kMaxCoverBytes - bytes->size();
        if (!oversize) bytes->append(reply->readAll());
        const auto networkError = reply->error();
        reply->deleteLater();
        if (m_cancelled) { completeAudio(lyricStatus, QStringLiteral("skipped")); return; }
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
        if ((format != "jpeg" && format != "jpg" && format != "png" && format != "webp")
            || !dimensions.isValid() || dimensions.width() > 4096 || dimensions.height() > 4096
            || reader.read().isNull()) {
            completeAudio(lyricStatus, QStringLiteral("error"));
            return;
        }
        const auto suffix = format == "jpeg" || format == "jpg" ? QStringLiteral(".jpg")
            : format == "png" ? QStringLiteral(".png") : QStringLiteral(".webp");
        const QFileInfo audio(m_audioPath);
        const auto path = QDir(audio.absolutePath()).filePath(audio.completeBaseName() + suffix);
        if (QFileInfo::exists(path)) { completeAudio(lyricStatus, QStringLiteral("existing")); return; }
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(*bytes) != bytes->size() || !file.commit()) {
            completeAudio(lyricStatus, QStringLiteral("error"));
            return;
        }
        completeAudio(lyricStatus, QStringLiteral("saved"));
    });
}

void KugouMusicService::completeAudio(const QString &lyricStatus, const QString &coverStatus)
{
    m_busy = false;
    m_downloadActive = false;
    emit audioReady(m_audioPath, lyricStatus, coverStatus, m_selected.title, m_selected.artist);
}

QString KugouMusicService::cancelDownload()
{
    if (!m_downloadActive) return QStringLiteral("No active Kugou download");
    m_cancelled = true;
    if (m_reply) m_reply->abort();
    else if (!m_audioPath.isEmpty()) finishAudio(QStringLiteral("skipped"));
    else { discardAudio(); finishOperation(QStringLiteral("kugou.download_cancelled")); }
    return {};
}

} // namespace nekotune
