#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

namespace nekotune {
/// Status exposes capability and progress without account secrets.
/// configured means the service/key prerequisites are available.
/// keySaved distinguishes a native-store key from a launch override.
/// loggedIn requires the account's usable token, user and device identity.
/// busy covers search/account/download work with one active operation.
/// downloadActive enables cancellation and guards managed-file cleanup.
struct KugouStatus {
    bool configured = false;
    bool keySaved = false;
    bool loggedIn = false;
    bool busy = false;
    bool downloadActive = false;
    QString credentialError;
};

/// hash is a provider identity, not the local audio SHA-256.
/// It addresses a search result and the provider's download route.
/// The eventual local import computes its own deduplication identity.
/// Duration is milliseconds even when the provider sends seconds.
/// Cover URLs are normalized before being exposed to the UI.
struct KugouSearchItem {
    QString hash;
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0;
    QUrl coverUrl;
};

enum class KugouEventType {
    CodeSent,
    LoggedIn,
    OperationFailed,
    SearchResults,
    DownloadProgress,
    DownloadStage,
    DownloadCancelled
};

/// Only fields relevant to type need values; others retain defaults.
/// SearchResults carries a bounded page of normalized song records.
/// DownloadProgress may have an unknown total while streaming.
/// DownloadStage describes optional assets after audio has been saved.
/// OperationFailed is a terminal event for the active operation.
/// These events carry public state, never cookies or account key bytes.
struct KugouEvent {
    KugouEventType type;
    QString message;
    QVector<KugouSearchItem> songs;
    int page = 1;
    qint64 received = 0;
    qint64 total = 0;
    QString stage;
};

/// Start methods report immediate validation/admission failures as text.
/// An empty returned string means the asynchronous operation was admitted.
/// Its eventual outcome arrives through eventReady or audioReady.
/// audioReady signals a saved file awaiting application-layer import.
/// It does not itself imply a successful library database transaction.
/// Account and provider details remain behind this typed interface.
class IKugouBackend : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual KugouStatus status() const = 0;
    virtual QString saveAccountKey(const QString &key) = 0;
    virtual QString clearAccountKey() = 0;
    virtual QString startCodeRequest(const QString &mobile) = 0;
    virtual QString startLogin(const QString &mobile, const QString &code) = 0;
    virtual QString startSearch(const QString &keywords, int page) = 0;
    virtual QString startDownload(const QString &hash) = 0;
    virtual QString cancelDownload() = 0;
  signals:
    void eventReady(const nekotune::KugouEvent &event);
    void audioReady(const QString &path, const QString &lyricStatus, const QString &coverStatus,
                    const QString &title, const QString &artist);
};
} // namespace nekotune
Q_DECLARE_METATYPE(nekotune::KugouEvent)
