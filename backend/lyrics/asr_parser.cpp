#include "lyrics/asr_parser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

namespace nekotune {
namespace {
qint64 timestamp(const QJsonValue &value)
{
    const double number = value.toDouble(-1);
    // Milliseconds, bounded to a week to reject corrupt/overflowing input.
    return value.isDouble() && std::isfinite(number) && number >= 0 && number <= 604800000
                   && std::floor(number) == number
               ? static_cast<qint64>(number)
               : -1;
}
} // namespace

QVector<LyricLine> AsrParser::parse(const QByteArray &json)
{
    const auto root = QJsonDocument::fromJson(json);
    if (!root.isObject())
        return {};
    // Use the first channel with valid sentences; combining channels duplicates vocals.
    for (const auto &transcript : root.object().value(QStringLiteral("transcripts")).toArray()) {
        QVector<LyricLine> lines;
        for (const auto &value : transcript.toObject().value(QStringLiteral("sentences")).toArray()) {
            const auto sentence = value.toObject();
            LyricLine line;
            line.timestampMs = timestamp(sentence.value(QStringLiteral("begin_time")));
            line.endTimestampMs = timestamp(sentence.value(QStringLiteral("end_time")));
            line.text = sentence.value(QStringLiteral("text")).toString();
            if (line.timestampMs < 0 || line.endTimestampMs <= line.timestampMs || line.text.trimmed().isEmpty())
                continue;
            qsizetype offset = 0;
            qint64 previousEnd = line.timestampMs;
            for (const auto &wordValue : sentence.value(QStringLiteral("words")).toArray()) {
                const auto word = wordValue.toObject();
                const auto text = word.value(QStringLiteral("text")).toString();
                const auto begin = timestamp(word.value(QStringLiteral("begin_time")));
                const auto end = timestamp(word.value(QStringLiteral("end_time")));
                const auto start = line.text.indexOf(text, offset);
                if (text.isEmpty() || start < 0 || begin < previousEnd || end <= begin
                    || end > line.endTimestampMs) {
                    // Keep the authoritative sentence intact if alignment is unreliable.
                    line.words.clear();
                    break;
                }
                auto length = text.size();
                const auto punctuation = word.value(QStringLiteral("punctuation")).toString();
                if (!punctuation.isEmpty() && line.text.mid(start + length, punctuation.size()) == punctuation)
                    length += punctuation.size();
                line.words.append({begin, end, start, length});
                offset = start + length;
                previousEnd = end;
            }
            lines.append(line);
        }
        if (!lines.isEmpty()) {
            std::stable_sort(lines.begin(), lines.end(),
                             [](const auto &a, const auto &b) { return a.timestampMs < b.timestampMs; });
            return lines;
        }
    }
    return {};
}

QString AsrParser::serialize(const QVector<LyricLine> &lines)
{
    QJsonArray sentences;
    for (const auto &line : lines) {
        QJsonArray words;
        for (const auto &word : line.words)
            words.append(QJsonObject{{QStringLiteral("begin_time"), word.timestampMs},
                                     {QStringLiteral("end_time"), word.endTimestampMs},
                                     {QStringLiteral("text"), line.text.mid(word.start, word.length)}});
        sentences.append(QJsonObject{{QStringLiteral("begin_time"), line.timestampMs},
                                     {QStringLiteral("end_time"), line.endTimestampMs},
                                     {QStringLiteral("text"), line.text},
                                     {QStringLiteral("words"), words}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("transcripts"), QJsonArray{QJsonObject{{QStringLiteral("sentences"), sentences}}}}})
                                 .toJson(QJsonDocument::Compact));
}

} // namespace nekotune
