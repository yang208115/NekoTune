#pragma once

#include "domain/lyrics/lyrics_types.h"
#include <QObject>

namespace nekotune {

/// Asynchronous provider seam. Replies echo the supplied token so callers can discard stale work.
/// Providers and their network objects must share the lyrics service's thread affinity.
class LyricsProvider : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    ~LyricsProvider() override = default;
    /// @param query Copied track metadata used for lookup or manual search.
    /// @param token Opaque attempt identity that every result/failure signal must echo.
    /// @param search Broader candidate search rather than a direct automatic lookup.
    /// The provider reports candidates; the service owns confidence and auto-selection policy.
    virtual void request(const LyricsQuery &query, quint64 token, bool search) = 0;
    virtual void cancel() = 0;
    virtual LyricsSource descriptor() const {
        return {QStringLiteral("lrclib"), QStringLiteral("LRCLIB"), true, false};
    }
    /// Resolve a selected candidate under the supplied new attempt token.
    /// The default provider already has the document and emits it immediately.
    /// Staged implementations override this to search another stage or download a payload.
    /// Callers must therefore tolerate both immediate and asynchronous result delivery.
    virtual void choose(const LyricsCandidate &candidate, quint64 token) {
        emit resolved(token, candidate.document);
    }

  signals:
    void resolved(quint64 token, const nekotune::LyricsDocument &document);
    void completed(quint64 token, const QVector<LyricsCandidate> &candidates);
    void failed(quint64 token, const QString &kind);
};

} // namespace nekotune
