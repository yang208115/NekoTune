#pragma once

#include "domain/lyrics/lyrics_provider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

namespace nekotune {

/// Implements direct lookup and broader search against LRCLIB.
/// Direct lookup includes album/duration evidence when available.
/// Broader search omits those restrictions to expose alternate releases.
/// The application ranks and checks confidence before auto-application.
/// One active reply is cancelled when a newer attempt starts.
/// Injected network managers and deadlines support deterministic tests.
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
