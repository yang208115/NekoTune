#pragma once
#include <QObject>
#include <QString>
#include <QVector>

namespace nekotune {
struct KugouStatus {
    bool configured = false;
    bool keySaved = false;
    bool loggedIn = false;
    bool busy = false;
    bool downloadActive = false;
};

struct KugouSearchItem {
    QString hash;
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0;
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

struct KugouEvent {
    KugouEventType type;
    QString message;
    QVector<KugouSearchItem> songs;
    int page = 1;
    qint64 received = 0;
    qint64 total = 0;
    QString stage;
};

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
