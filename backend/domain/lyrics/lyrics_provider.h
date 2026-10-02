#pragma once

#include "domain/lyrics/lyrics_types.h"
#include <QObject>

namespace nekotune {

class LyricsProvider : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    ~LyricsProvider() override = default;
    virtual void request(const LyricsQuery &query, quint64 token, bool search) = 0;
    virtual void cancel() = 0;
    virtual LyricsSource descriptor() const {
        return {QStringLiteral("lrclib"), QStringLiteral("LRCLIB"), true, false};
    }
    virtual void choose(const LyricsCandidate &candidate, quint64 token) {
        emit resolved(token, candidate.document);
    }

  signals:
    void resolved(quint64 token, const nekotune::LyricsDocument &document);
    void completed(quint64 token, const QVector<LyricsCandidate> &candidates);
    void failed(quint64 token, const QString &kind);
};

} // namespace nekotune
