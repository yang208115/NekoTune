#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <optional>

namespace nekotune {

struct KrcWord {
    qint64 offsetMs = 0;
    qint64 timestampMs = 0;
    qint64 durationMs = 0;
    QString text;
};

struct KrcLine {
    qint64 timestampMs = 0;
    qint64 durationMs = 0;
    QString text;
    QVector<KrcWord> words;
};

struct KrcPayload {
    QByteArray binary;
    QString text;
};

class KrcParser {
  public:
    static std::optional<QString> decode(const QByteArray &binary);
    static std::optional<QString> read(const QByteArray &data);
    static QVector<KrcLine> parse(const QString &text);
};

} // namespace nekotune
