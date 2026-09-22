#pragma once

#include "lyrics/lyrics_types.h"
#include <QObject>

namespace nekotune {

class LyricsProvider : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    ~LyricsProvider() override = default;
    virtual void request(const LyricsQuery &query, quint64 token, bool search) = 0;
    virtual void cancel() = 0;

  signals:
    void completed(quint64 token, const QVector<LyricsCandidate> &candidates);
    void failed(quint64 token, const QString &kind);
};

} // namespace nekotune
