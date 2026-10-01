#include "lyrics/krc_parser.h"

#include <QRegularExpression>
#include <QStringDecoder>
#include <algorithm>
#include <array>
#include <limits>
#include <zlib.h>

namespace nekotune {
namespace {
constexpr qsizetype kMaxDecodedBytes = 16 * 1024 * 1024;
constexpr std::array<quint8, 16> kKey{0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47,
                                      0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69};

bool number(const QString &value, qint64 &output)
{
    bool ok = false;
    const auto parsed = value.toLongLong(&ok);
    if (ok) output = parsed;
    return ok;
}
} // namespace

std::optional<QString> KrcParser::decode(const QByteArray &binary)
{
    if (!binary.startsWith("krc1") || binary.size() <= 4 || binary.size() > 4 * 1024 * 1024)
        return std::nullopt;
    QByteArray compressed = binary.mid(4);
    for (qsizetype index = 0; index < compressed.size(); ++index)
        compressed[index] = char(quint8(compressed.at(index)) ^ kKey[std::size_t(index) % kKey.size()]);

    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(compressed.data());
    stream.avail_in = uInt(compressed.size());
    if (inflateInit(&stream) != Z_OK) return std::nullopt;
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
    const bool complete = stream.avail_in == 0;
    inflateEnd(&stream);
    if (!complete) return std::nullopt;
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(output);
    if (decoder.hasError()) return std::nullopt;
    if (text.startsWith(QChar::ByteOrderMark)) text.remove(0, 1);
    return text;
}

std::optional<QString> KrcParser::read(const QByteArray &data)
{
    if (data.startsWith("krc1")) return decode(data);
    if (data.size() > kMaxDecodedBytes) return std::nullopt;
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(data);
    if (decoder.hasError()) return std::nullopt;
    if (text.startsWith(QChar::ByteOrderMark)) text.remove(0, 1);
    return text;
}

std::optional<KrcPayload> KrcParser::fromApi(const QJsonObject &response)
{
    const auto content = response.value(QStringLiteral("content"));
    if (response.value(QStringLiteral("status")).toInt() != 200 || !content.isString())
        return std::nullopt;
    const auto encoded = content.toString().toLatin1();
    if (encoded.isEmpty() || encoded.size() > 4 * 1024 * 1024) return std::nullopt;
    const auto result = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (result.decodingStatus != QByteArray::Base64DecodingStatus::Ok) return std::nullopt;
    auto text = decode(result.decoded);
    if (!text || parse(*text).isEmpty()) return std::nullopt;
    return KrcPayload{result.decoded, *text};
}

QVector<KrcLine> KrcParser::parse(const QString &text)
{
    static const QRegularExpression linePattern(QStringLiteral(R"(^\[(\d+),(\d+)\](.*)$)"));
    static const QRegularExpression wordPattern(QStringLiteral(R"(<(-?\d+),(\d+),(-?\d+)>)"));
    static const QRegularExpression offsetPattern(QStringLiteral(R"(^\[offset:([+-]?\d+)\]$)"),
                                                  QRegularExpression::CaseInsensitiveOption);
    const auto sourceLines = text.split(QLatin1Char('\n'));
    qint64 offset = 0;
    for (const auto &source : sourceLines) {
        const auto match = offsetPattern.match(source.trimmed());
        if (match.hasMatch()) number(match.captured(1), offset);
    }
    QVector<KrcLine> result;
    for (const auto &source : sourceLines) {
        const auto lineSource = source.endsWith(QLatin1Char('\r')) ? source.chopped(1) : source;
        const auto match = linePattern.match(lineSource);
        if (!match.hasMatch()) continue;
        qint64 start = 0, duration = 0;
        if (!number(match.captured(1), start) || !number(match.captured(2), duration) ||
            (offset > 0 && start > std::numeric_limits<qint64>::max() - offset))
            continue;
        KrcLine line;
        line.timestampMs = std::max<qint64>(0, start + offset);
        line.durationMs = duration;
        const auto body = match.captured(3);
        auto wordMatch = wordPattern.globalMatch(body);
        QVector<QRegularExpressionMatch> matches;
        while (wordMatch.hasNext()) matches.append(wordMatch.next());
        for (int index = 0; index < matches.size(); ++index) {
            const auto &item = matches.at(index);
            qint64 wordOffset = 0, wordDuration = 0;
            if (!number(item.captured(1), wordOffset) || !number(item.captured(2), wordDuration) ||
                wordOffset > std::numeric_limits<qint64>::max() - line.timestampMs)
                continue;
            const auto end = index + 1 < matches.size() ? matches.at(index + 1).capturedStart() : body.size();
            const auto wordText = (index == 0 ? body.left(item.capturedStart()) : QString())
                                  + body.mid(item.capturedEnd(), end - item.capturedEnd());
            line.words.append({wordOffset, std::max<qint64>(0, line.timestampMs + wordOffset), wordDuration, wordText});
            line.text += wordText;
        }
        if (line.words.isEmpty()) line.text = body;
        if (!line.text.isEmpty()) result.append(line);
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const KrcLine &a, const KrcLine &b) { return a.timestampMs < b.timestampMs; });
    return result;
}

QJsonArray KrcParser::toJson(const QVector<KrcLine> &lines)
{
    QJsonArray result;
    for (const auto &line : lines) {
        QJsonArray words;
        for (const auto &word : line.words)
            words.append(QJsonObject{{QStringLiteral("text"), word.text},
                                     {QStringLiteral("offset_ms"), word.offsetMs},
                                     {QStringLiteral("time_ms"), word.timestampMs},
                                     {QStringLiteral("duration_ms"), word.durationMs}});
        result.append(QJsonObject{{QStringLiteral("time_ms"), line.timestampMs},
                                  {QStringLiteral("duration_ms"), line.durationMs},
                                  {QStringLiteral("text"), line.text},
                                  {QStringLiteral("words"), words}});
    }
    return result;
}

} // namespace nekotune
