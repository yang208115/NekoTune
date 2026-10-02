#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <optional>

namespace nekotune {

struct KrcWord {
    // offsetMs is relative to its line; timestampMs includes the line/global offset for UI seeking.
    qint64 offsetMs = 0;
    qint64 timestampMs = 0;
    qint64 durationMs = 0;
    QString text;
};

/// Line timestamps include the global offset and are clamped at zero.
/// Words retain their own duration and line-relative offset.
/// The concatenated text also supports plain-text display and AI input.
/// A line without word tags can still be parsed as a timed text line.
/// The frontend consumes absolute word times rather than reapplying offsets.
struct KrcLine {
    qint64 timestampMs = 0;
    qint64 durationMs = 0;
    QString text;
    QVector<KrcWord> words;
};

/// Keep both representations after validating a downloaded response.
/// binary preserves the provider's original compressed KRC sidecar.
/// text is decoded UTF-8 for immediate parsing and lyric presentation.
/// Callers can save the binary without recompressing or losing timing.
struct KrcPayload {
    QByteArray binary;
    QString text;
};

class KrcParser {
  public:
    /// decode accepts krc1/XOR/zlib bytes; read also accepts decoded UTF-8 sidecars.
    /// Invalid encoding, truncated streams or oversized expansion return no value.
    static std::optional<QString> decode(const QByteArray &binary);
    /// @param data Compressed provider payload or already decoded UTF-8 sidecar bytes.
    /// @return Valid decoded text, or no value for unsupported/malformed/oversized input.
    /// This handles encoding; parse separately determines whether timed lyric rows exist.
    static std::optional<QString> read(const QByteArray &data);
    /// @param text Decoded KRC text, including optional document offset metadata.
    /// @return Stable chronological line order with absolute word timestamps in milliseconds.
    /// Malformed rows are skipped; a valid line can survive without word-level markers.
    /// Word text preserves whitespace for layout and plain-text reconstruction.
    static QVector<KrcLine> parse(const QString &text);
};

} // namespace nekotune
