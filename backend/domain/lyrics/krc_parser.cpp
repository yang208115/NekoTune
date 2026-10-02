#include "domain/lyrics/krc_parser.h"

#include <QRegularExpression>
#include <QStringDecoder>
#include <algorithm>
#include <array>
#include <limits>
#include <zlib.h>

namespace nekotune {
namespace {
// Compressed input size alone cannot bound memory use; cap expansion while inflating untrusted KRC.
constexpr qsizetype kMaxDecodedBytes = 16 * 1024 * 1024;
constexpr std::array<quint8, 16> kKey{0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47,
                                      0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69};

bool number(const QString &value, qint64 &output) {
    bool ok = false;
    const auto parsed = value.toLongLong(&ok);
    if (ok)
        output = parsed;
    return ok;
}
} // namespace

std::optional<QString> KrcParser::decode(const QByteArray &binary) {
    if (!binary.startsWith("krc1") || binary.size() <= 4 || binary.size() > 4 * 1024 * 1024)
        return std::nullopt;
    // The four-byte magic is framing; XOR applies only to the following compressed payload.
    // Keep the key position relative to that payload rather than the original file offset.
    // Decode into a separate buffer so callers can retain the original binary cache entry.
    QByteArray compressed = binary.mid(4);
    for (qsizetype index = 0; index < compressed.size(); ++index)
        compressed[index] = char(quint8(compressed.at(index)) ^ kKey[std::size_t(index) % kKey.size()]);

    // Inflation uses fixed-size output chunks instead of trusting a length from provider data.
    // The decoded-byte ceiling is enforced before appending each chunk.
    // Every initialized stream is ended on both success and malformed-input exits.
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(compressed.data());
    stream.avail_in = uInt(compressed.size());
    if (inflateInit(&stream) != Z_OK)
        return std::nullopt;
    QByteArray output;
    std::array<char, 16384> chunk{};
    int status = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
        stream.avail_out = uInt(chunk.size());
        status = inflate(&stream, Z_NO_FLUSH);
        const auto written = qsizetype(chunk.size() - stream.avail_out);
        if ((status != Z_OK && status != Z_STREAM_END) || output.size() + written > kMaxDecodedBytes) {
            inflateEnd(&stream);
            return std::nullopt;
        }
        output.append(chunk.data(), written);
    } while (status != Z_STREAM_END);
    // A valid first zlib stream is insufficient if unexplained trailing input remains.
    // Reject the whole payload rather than accepting a truncated or concatenated envelope.
    // UTF-8 validation happens only after this compression boundary has been checked.
    const bool complete = stream.avail_in == 0;
    inflateEnd(&stream);
    if (!complete)
        return std::nullopt;
    // Invalid UTF-8 is a format failure, not text to repair through replacement characters.
    // Removing a BOM preserves timestamp recognition at the start of the first source line.
    // The same text contract is used by the uncompressed read path below.
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(output);
    if (decoder.hasError())
        return std::nullopt;
    if (text.startsWith(QChar::ByteOrderMark))
        text.remove(0, 1);
    return text;
}

std::optional<QString> KrcParser::read(const QByteArray &data) {
    if (data.startsWith("krc1"))
        return decode(data);
    if (data.size() > kMaxDecodedBytes)
        return std::nullopt;
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(data);
    if (decoder.hasError())
        return std::nullopt;
    if (text.startsWith(QChar::ByteOrderMark))
        text.remove(0, 1);
    return text;
}

QVector<KrcLine> KrcParser::parse(const QString &text) {
    static const QRegularExpression linePattern(QStringLiteral(R"(^\[(\d+),(\d+)\](.*)$)"));
    static const QRegularExpression wordPattern(QStringLiteral(R"(<(-?\d+),(\d+),(-?\d+)>)"));
    static const QRegularExpression offsetPattern(QStringLiteral(R"(^\[offset:([+-]?\d+)\]$)"),
                                                  QRegularExpression::CaseInsensitiveOption);
    const auto sourceLines = text.split(QLatin1Char('\n'));
    // Offset is document-wide even when its metadata tag follows timed lyric rows.
    // Read it in a first pass so all line timestamps use one adjustment.
    // The final successfully parsed offset tag takes precedence.
    qint64 offset = 0;
    for (const auto &source : sourceLines) {
        const auto match = offsetPattern.match(source.trimmed());
        if (match.hasMatch())
            number(match.captured(1), offset);
    }
    QVector<KrcLine> result;
    for (const auto &source : sourceLines) {
        const auto lineSource = source.endsWith(QLatin1Char('\r')) ? source.chopped(1) : source;
        const auto match = linePattern.match(lineSource);
        if (!match.hasMatch())
            continue;
        qint64 start = 0, duration = 0;
        if (!number(match.captured(1), start) || !number(match.captured(2), duration) ||
            (offset > 0 && start > std::numeric_limits<qint64>::max() - offset))
            continue;
        KrcLine line;
        // Negative offsets are allowed; clamp before time zero and reject positive overflow above.
        line.timestampMs = std::max<qint64>(0, start + offset);
        line.durationMs = duration;
        const auto body = match.captured(3);
        auto wordMatch = wordPattern.globalMatch(body);
        QVector<QRegularExpressionMatch> matches;
        while (wordMatch.hasNext())
            matches.append(wordMatch.next());
        for (int index = 0; index < matches.size(); ++index) {
            const auto &item = matches.at(index);
            qint64 wordOffset = 0, wordDuration = 0;
            if (!number(item.captured(1), wordOffset) || !number(item.captured(2), wordDuration) ||
                wordOffset > std::numeric_limits<qint64>::max() - line.timestampMs)
                continue;
            // Word text spans from this timing marker to the next marker, not to the next space.
            // This retains punctuation, inter-word spaces and trailing whitespace in the source.
            // Untimed text before the first marker is attached to that first word.
            const auto end = index + 1 < matches.size() ? matches.at(index + 1).capturedStart() : body.size();
            const auto wordText = (index == 0 ? body.left(item.capturedStart()) : QString()) +
                                  body.mid(item.capturedEnd(), end - item.capturedEnd());
            line.words.append(
                {wordOffset, std::max<qint64>(0, line.timestampMs + wordOffset), wordDuration, wordText});
            line.text += wordText;
        }
        // A timed line without accepted word markers still supplies useful line-level lyrics.
        // Do not reject it solely because word-level progression cannot be built.
        // Empty text is omitted after this fallback so the UI receives actual lyric rows.
        if (line.words.isEmpty())
            line.text = body;
        if (!line.text.isEmpty())
            result.append(line);
    }
    // Provider input is not required to be chronologically ordered.
    // Stable ordering preserves source precedence for duplicate line timestamps.
    // The frontend's upper-bound search relies on this ordering contract.
    std::stable_sort(result.begin(), result.end(),
                     [](const KrcLine &a, const KrcLine &b) { return a.timestampMs < b.timestampMs; });
    return result;
}

} // namespace nekotune
