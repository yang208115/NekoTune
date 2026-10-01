#pragma once

#include "domain/lyrics_provider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

namespace nekotune {

class LrclibProvider final : public LyricsProvider {
    Q_OBJECT
  public:
    explicit LrclibProvider(QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr,
                            int deadlineMs = 12000);
    ~LrclibProvider() override;
    void request(const LyricsQuery &query, quint64 token, bool search) override;
    void cancel() override;

  private:
    QNetworkAccessManager *m_manager;
    QPointer<QNetworkReply> m_reply;
    int m_deadlineMs;
};

} // namespace nekotune
