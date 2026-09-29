#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace nekotune {
class AliyunAsr final : public QObject {
    Q_OBJECT
  public:
    explicit AliyunAsr(QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr, int pollIntervalMs = 2000,
                       int requestTimeoutMs = 60000, int totalTimeoutMs = 1800000);
    ~AliyunAsr() override;
    static bool supportsModel(const QString &model);
    void start(const QString &path, const QString &apiKey, const QString &language,
               const QString &model = QStringLiteral("fun-asr"));
    void cancel();
    bool active() const
    {
        return m_active;
    }
  signals:
    void progress(const QString &stage);
    void taskSubmitted(const QString &taskId, const QString &model, const QString &language);
    void completed(const QByteArray &transcription);
    void failed(const QString &code);

  private:
    QNetworkRequest apiRequest(const QString &path) const;
    void watch(QNetworkReply *reply, const QString &stage, std::function<void(const QByteArray &)> next);
    void upload(const QJsonObject &policy);
    void submit(const QString &ossUrl);
    void poll();
    void fail(const QString &code);
    QNetworkAccessManager *m_manager;
    QPointer<QNetworkReply> m_reply;
    QTimer m_pollTimer;
    QTimer m_totalTimer;
    QString m_path;
    QString m_key;
    QString m_language;
    QString m_model;
    QString m_task;
    QString m_operationId;
    QString m_stage;
    QString m_lastTaskState;
    QElapsedTimer m_elapsed;
    int m_requestTimeoutMs;
    int m_totalTimeoutMs;
    bool m_active = false;
};
} // namespace nekotune
