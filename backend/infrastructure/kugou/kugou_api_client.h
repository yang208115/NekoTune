#pragma once
#include "infrastructure/kugou/kugou_account_session.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <functional>
namespace nekotune {
class KugouApiClient final : public QObject {
    Q_OBJECT
  public:
    using JsonCallback = std::function<void(const QJsonObject &, const QString &)>;
    KugouApiClient(QNetworkAccessManager &manager, QUrl baseUrl, KugouAccountSession &account)
        : m_manager(&manager), m_baseUrl(std::move(baseUrl)), m_account(account) {}
    void requestJson(const QString &route, const QJsonObject &body, JsonCallback callback);
    void accountRequest(const QString &route, const QJsonObject &body, JsonCallback callback);
    void cancel();

  private:
    QNetworkAccessManager *m_manager;
    QUrl m_baseUrl;
    KugouAccountSession &m_account;
    QPointer<QNetworkReply> m_reply;
    bool m_cancelled = false;
};
} // namespace nekotune
