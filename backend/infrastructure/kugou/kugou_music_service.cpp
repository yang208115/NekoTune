#include "infrastructure/kugou/kugou_music_service.h"
#include "app_paths.h"
#include "infrastructure/kugou/kugou_settings.h"

#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

namespace nekotune {
namespace {
const QRegularExpression kHashPattern(QStringLiteral("^[a-fA-F0-9]{32}$"));
const QRegularExpression kMobilePattern(QStringLiteral("^1[3-9][0-9]{9}$"));
const QRegularExpression kCodePattern(QStringLiteral("^[0-9]{4,8}$"));

QString jsonString(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toInteger());
    return {};
}

bool validDfid(const QString &value) {
    return !value.isEmpty() && value != QStringLiteral("0") && value != QStringLiteral("-") &&
           value != QStringLiteral("undefined");
}

} // namespace

KugouMusicService::KugouMusicService(
    QObject *parent, QNetworkAccessManager *manager, const QUrl &baseUrl, const QString &sessionPath,
    const QString &musicDirectory, const QString &keyPath,
    std::function<Result<QString>(const QString &, const QString &)> destination)
    : IKugouBackend(parent), m_manager(manager ? manager : new QNetworkAccessManager(this)) {
    qRegisterMetaType<KugouEvent>();
    auto settings = KugouSettings::load();
    if (!baseUrl.isEmpty())
        settings.workerUrl = baseUrl;
    m_enabled = settings.enabled;
    m_baseUrl = settings.workerUrl;
    m_configurationError = settings.unavailableReason();
    m_baseUrl.setPath({});
    m_destination = std::move(destination);
    QString directoryError;
    if (!AppPaths::prepare(&directoryError))
        m_configurationError = directoryError;
    m_keyPath = keyPath.isEmpty() ? AppPaths::configFile("kugou-account-key") : keyPath;
    m_sessionPath = sessionPath.isEmpty() ? AppPaths::configFile("kugou-session.json") : sessionPath;
    m_musicDirectory = musicDirectory.isEmpty() ? AppPaths::musicDirectory() : musicDirectory;
    m_account = std::make_unique<KugouAccountSession>(m_keyPath, m_sessionPath);
    m_api = std::make_unique<KugouApiClient>(*m_manager, m_baseUrl, *m_account);
    m_download = std::make_unique<KugouDownloadJob>(*m_manager, *m_api, m_musicDirectory);
    connect(m_download.get(), &KugouDownloadJob::eventReady, this, [this](const KugouEvent &event) {
        if (event.type == KugouEventType::OperationFailed ||
            event.type == KugouEventType::DownloadCancelled) {
            m_busy = false;
            m_downloadActive = false;
        }
        emit eventReady(event);
    });
    connect(m_download.get(), &KugouDownloadJob::audioReady, this,
            [this](const QString &path, const QString &lyric, const QString &cover, const QString &title,
                   const QString &artist) {
                m_busy = false;
                m_downloadActive = false;
                emit audioReady(path, lyric, cover, title, artist);
            });
}

KugouMusicService::~KugouMusicService() { shutdown(); }
void KugouMusicService::shutdown() {
    m_cancelled = true;
    m_api->cancel();
    m_download->shutdown();
    m_busy = false;
    m_downloadActive = false;
}
QString KugouMusicService::saveConfiguration(bool enabled, const QString &workerUrl) {
    if (m_busy)
        return QStringLiteral("Kugou operation already in progress");
    KugouSettings settings{enabled, QUrl(workerUrl.trimmed(), QUrl::StrictMode)};
    const auto error = settings.validationError();
    if (!error.isEmpty())
        return error;
    settings.workerUrl.setPath({});
    if (!AppPaths::saveSetting("kugou", QJsonObject{
            {"enabled", enabled}, {"worker_url", settings.workerUrl.toString(QUrl::FullyEncoded)}}))
        return QStringLiteral("Unable to save Kugou settings");
    m_enabled = enabled;
    m_baseUrl = settings.workerUrl;
    m_configurationError = settings.unavailableReason();
    m_api->setBaseUrl(m_baseUrl);
    m_songs.clear();
    emit eventReady({.type = KugouEventType::ConfigChanged});
    return {};
}

QString KugouMusicService::saveAccountKey(const QString &key) {
    return m_busy ? QStringLiteral("Kugou operation already in progress") : m_account->saveAccountKey(key);
}
QString KugouMusicService::clearAccountKey() {
    return m_busy ? QStringLiteral("Kugou operation already in progress") : m_account->clearAccountKey();
}
QString KugouMusicService::cancelDownload() {
    if (!m_downloadActive)
        return QStringLiteral("No active Kugou download");
    m_cancelled = true;
    m_api->cancel();
    return m_download->cancelDownload();
}

KugouStatus KugouMusicService::status() const {
    return {m_configurationError.isEmpty() && !m_account->key.isEmpty(), m_account->keySaved,
            m_enabled && m_configurationError.isEmpty() &&
                !m_account->cookies.value(QStringLiteral("token")).isEmpty() &&
                !m_account->cookies.value(QStringLiteral("userid")).isEmpty() &&
                validDfid(m_account->cookies.value(QStringLiteral("dfid"))),
            m_busy, m_downloadActive,
            !m_account->keyError.isEmpty() ? m_account->keyError : m_account->sessionError,
            m_enabled, m_baseUrl.toString(QUrl::FullyEncoded)};
}

bool KugouMusicService::businessOk(const QJsonObject &body, const QString &expected) {
    const auto code = body.value(QStringLiteral("error_code"));
    const auto other = body.value(QStringLiteral("errcode"));
    return jsonString(body.value(QStringLiteral("status"))) == expected &&
           (code.isUndefined() || jsonString(code) == QStringLiteral("0")) &&
           (other.isUndefined() || jsonString(other) == QStringLiteral("0"));
}

void KugouMusicService::finishOperation(KugouEventType event, const QString &message) {
    m_busy = false;
    m_downloadActive = false;
    emit eventReady({.type = event, .message = message});
}

// SMS requires both a configured Worker key and valid mobile syntax.
// Register a device identity first when the session lacks usable dfid.
// Only after registration has been persisted may the SMS call proceed.
// The busy flag spans both network stages as one logical operation.
// User-visible admission errors happen before any request is issued.
QString KugouMusicService::startCodeRequest(const QString &mobile) {
    if (!m_configurationError.isEmpty())
        return m_configurationError;
    if (m_account->key.isEmpty())
        return QStringLiteral("Set the Kugou account key in Settings or the launch environment");
    if (m_busy)
        return QStringLiteral("Kugou operation already in progress");
    if (!kMobilePattern.match(mobile).hasMatch())
        return QStringLiteral("Invalid mainland China mobile number");
    m_busy = true;
    m_cancelled = false;
    auto send = [this, mobile]() {
        m_api->accountRequest(QStringLiteral("/captcha/sent"), {{QStringLiteral("mobile"), mobile}},
                              [this](const QJsonObject &result, const QString &error) {
                                  finishOperation(error.isEmpty() && businessOk(result, QStringLiteral("1"))
                                                      ? KugouEventType::CodeSent
                                                      : KugouEventType::OperationFailed,
                                                  error.isEmpty() && !businessOk(result, QStringLiteral("1"))
                                                      ? QStringLiteral("Kugou rejected SMS request")
                                                      : error);
                              });
    };
    const auto dfid = m_account->cookies.value(QStringLiteral("dfid"));
    if (validDfid(dfid)) {
        send();
    } else {
        m_api->accountRequest(QStringLiteral("/register/dev"), {{QStringLiteral("cookies"), QJsonObject{}}},
                              [this, send](const QJsonObject &result, const QString &error) {
                                  if (!error.isEmpty() || !businessOk(result, QStringLiteral("1")) ||
                                      !validDfid(m_account->cookies.value(QStringLiteral("dfid")))) {
                                      finishOperation(KugouEventType::OperationFailed,
                                                      error.isEmpty()
                                                          ? QStringLiteral("Kugou device registration failed")
                                                          : error);
                                      return;
                                  }
                                  send();
                              });
    }
    return {};
}

// Login success requires more than a nominal provider success code.
// Returned token/user identity must agree with the persisted cookies.
// Otherwise the UI would claim a session that download cannot use.
// Extra-verification responses remain operation failures for user action.
QString KugouMusicService::startLogin(const QString &mobile, const QString &code) {
    if (m_account->key.isEmpty() || !m_configurationError.isEmpty())
        return QStringLiteral("Kugou account Worker is not configured");
    if (m_busy)
        return QStringLiteral("Kugou operation already in progress");
    if (!kMobilePattern.match(mobile).hasMatch() || !kCodePattern.match(code).hasMatch())
        return QStringLiteral("Invalid mobile number or verification code");
    m_busy = true;
    m_cancelled = false;
    m_api->accountRequest(
        QStringLiteral("/login/cellphone"),
        {{QStringLiteral("mobile"), mobile}, {QStringLiteral("code"), code}},
        [this](const QJsonObject &result, const QString &error) {
            const auto data = result.value(QStringLiteral("data")).toObject();
            const bool valid = businessOk(result, QStringLiteral("1")) &&
                               !data.value(QStringLiteral("token")).toString().isEmpty() &&
                               !jsonString(data.value(QStringLiteral("userid"))).isEmpty() &&
                               m_account->cookies.value(QStringLiteral("token")) ==
                                   data.value(QStringLiteral("token")).toString() &&
                               m_account->cookies.value(QStringLiteral("userid")) ==
                                   jsonString(data.value(QStringLiteral("userid")));
            finishOperation(
                error.isEmpty() && valid ? KugouEventType::LoggedIn : KugouEventType::OperationFailed,
                error.isEmpty() && !valid ? QStringLiteral("Kugou login failed or needs extra verification")
                                          : error);
        });
    return {};
}

QString KugouMusicService::startSearch(const QString &keywords, int page) {
    if (!m_configurationError.isEmpty())
        return m_configurationError;
    if (m_busy)
        return QStringLiteral("Kugou operation already in progress");
    if (keywords.trimmed().isEmpty() || keywords.size() > 200 || page < 1 || page > 1000)
        return QStringLiteral("Invalid search keywords or page");
    m_busy = true;
    m_cancelled = false;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("keywords"), keywords.trimmed());
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("song"));
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("pagesize"), QStringLiteral("30"));
    m_api->requestJson(
        QStringLiteral("/search?") + query.toString(QUrl::FullyEncoded), {},
        [this, page](const QJsonObject &result, const QString &error) {
            const auto rows =
                result.value(QStringLiteral("data")).toObject().value(QStringLiteral("lists")).toArray();
            if (!error.isEmpty() || !businessOk(result, QStringLiteral("1")) ||
                !result.value(QStringLiteral("data")).toObject().value(QStringLiteral("lists")).isArray()) {
                finishOperation(KugouEventType::OperationFailed,
                                error.isEmpty() ? QStringLiteral("Kugou search failed") : error);
                return;
            }
            m_songs.clear();
            QVector<KugouSearchItem> songs;
            auto add = [this, &songs](const QJsonObject &item, const QJsonObject &fallback) {
                KugouSong song;
                song.hash = item.value(QStringLiteral("FileHash"))
                                .toString(item.value(QStringLiteral("hash")).toString());
                if (!kHashPattern.match(song.hash).hasMatch() || m_songs.contains(song.hash))
                    return;
                song.title = item.value(QStringLiteral("SongName"))
                                 .toString(fallback.value(QStringLiteral("SongName")).toString());
                song.artist = item.value(QStringLiteral("SingerName"))
                                  .toString(fallback.value(QStringLiteral("SingerName")).toString());
                song.album = item.value(QStringLiteral("AlbumName"))
                                 .toString(fallback.value(QStringLiteral("AlbumName")).toString());
                const auto itemImage = item.value(QStringLiteral("Image")).toString();
                song.coverUrl = KugouDownloadJob::trustedCoverUrl(
                    itemImage.isEmpty() ? fallback.value(QStringLiteral("Image")).toString() : itemImage);
                for (auto *field : {&song.title, &song.artist, &song.album}) {
                    field->remove(QRegularExpression(QStringLiteral("<[^>]*>")));
                    *field = field->trimmed().left(256);
                }
                song.audioId = jsonString(item.value(QStringLiteral("MixSongID")));
                if (!QRegularExpression(QStringLiteral("^[0-9]{1,15}$")).match(song.audioId).hasMatch())
                    song.audioId.clear();
                song.durationMs =
                    qMax<qint64>(0, qRound64(item.value(QStringLiteral("Duration")).toDouble() * 1000));
                m_songs.insert(song.hash, song);
                songs.append(song);
            };
            for (const auto &value : rows) {
                const auto row = value.toObject();
                add(row, row);
                for (const auto &variant : row.value(QStringLiteral("Grp")).toArray())
                    add(variant.toObject(), row);
                if (songs.size() >= 100)
                    break;
            }
            m_busy = false;
            emit eventReady({.type = KugouEventType::SearchResults, .songs = songs, .page = page});
        });
    return {};
}

// Require the current search result so provider IDs and metadata agree.
// Reserve a destination before asking for the temporary audio URL.
// A previous completed audio file can be reused without overwriting it.
// Optional assets can still be attempted on that reusable file.
// The URL response supports several provider envelope shapes.
// Every extracted URL still passes the same trusted-audio validation.
// Entitlement/verification errors are surfaced before transfer starts.
QString KugouMusicService::startDownload(const QString &hash) {
    if (!m_configurationError.isEmpty() || m_account->key.isEmpty())
        return QStringLiteral("Kugou account Worker is not configured");
    if (m_busy)
        return QStringLiteral("Kugou operation already in progress");
    if (!status().loggedIn)
        return QStringLiteral("Log in to Kugou first");
    if (!m_songs.contains(hash))
        return QStringLiteral("Search for the song again before downloading");
    QString destinationBase;
    if (m_destination) {
        auto destination = m_destination(hash, m_songs.value(hash).title);
        if (!destination)
            return destination.error().message;
        destinationBase = destination.value();
    }
    m_selected = m_songs.value(hash);
    m_busy = true;
    m_downloadActive = true;
    m_cancelled = false;
    m_download->prepare(m_selected, destinationBase);
    if (m_download->reuseExisting())
        return {};
    QJsonObject body{{QStringLiteral("hash"), hash}, {QStringLiteral("quality"), QStringLiteral("128")}};
    if (!m_selected.audioId.isEmpty())
        body.insert(QStringLiteral("album_audio_id"), m_selected.audioId);
    m_api->accountRequest(
        QStringLiteral("/song/url"), body, [this](const QJsonObject &result, const QString &error) {
            if (m_cancelled) {
                finishOperation(KugouEventType::DownloadCancelled);
                return;
            }
            const auto urls = result.value(QStringLiteral("url"));
            const auto nested = result.value(QStringLiteral("data")).toObject().value(QStringLiteral("url"));
            const auto candidate =
                urls.isArray()    ? (urls.toArray().isEmpty() ? QString() : urls.toArray().at(0).toString())
                : urls.isString() ? urls.toString()
                : nested.isArray()
                    ? (nested.toArray().isEmpty() ? QString() : nested.toArray().at(0).toString())
                    : nested.toString();
            const QUrl url(candidate);
            if (!error.isEmpty() ||
                !(businessOk(result, QStringLiteral("1")) || businessOk(result, QStringLiteral("200"))) ||
                !KugouDownloadJob::trustedAudioUrl(url)) {
                const auto code = jsonString(result.value(QStringLiteral("error_code")));
                const auto failure = result.value(QStringLiteral("fail_process")).toArray();
                const QString message =
                    !error.isEmpty() ? error
                    : code == QStringLiteral("20028")
                        ? QStringLiteral("Kugou requires additional account verification")
                    : failure.contains(QStringLiteral("pkg")) || failure.contains(QStringLiteral("buy"))
                        ? QStringLiteral("Kugou account has no playback permission for this song")
                        : QStringLiteral("Kugou did not provide a trusted audio URL");
                finishOperation(KugouEventType::OperationFailed, message);
                return;
            }
            m_download->beginAudio(url);
        });
    return {};
}

} // namespace nekotune
