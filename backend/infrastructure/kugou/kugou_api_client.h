#pragma once
#include "infrastructure/kugou/kugou_account_session.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <functional>
namespace nekotune {
/// One-reply JSON transport used by the admitted account/search operation.
/// Empty request bodies select anonymous GET, nonempty bodies select POST.
/// Only POST sends the account admission key to the configured Worker.
/// Account requests additionally attach backend-private cookie state.
/// Returned cookies are removed before the public callback receives data.
/// The owning service enforces operation admission and handles cancellation.
class KugouApiClient final : public QObject {
    Q_OBJECT
  public:
    using JsonCallback = std::function<void(const QJsonObject &, const QString &)>;
    KugouApiClient(QNetworkAccessManager &manager, QUrl baseUrl, KugouAccountSession &account)
        : m_manager(&manager), m_baseUrl(std::move(baseUrl)), m_account(account) {}
    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }
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
