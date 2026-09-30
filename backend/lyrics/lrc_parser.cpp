#include "lyrics/lrc_parser.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>

namespace nekotune {

QVector<LyricLine> LrcParser::parse(const QString &lyrics)
{
    static const QRegularExpression timestamp(QStringLiteral(R"(^\[(\d{1,6}):([0-5]\d)(?:[.:](\d{1,3}))?\])"));
    static const QRegularExpression offsetTag(QStringLiteral(R"(^\[offset:([+-]?\d{1,9})\]$)"),
                                              QRegularExpression::CaseInsensitiveOption);
    auto sourceLines = lyrics.split(QLatin1Char('\n'));
    qint64 offset = 0;
    for (const auto &line : sourceLines) {
        const auto match = offsetTag.match(line.trimmed());
        if (match.hasMatch())
            offset = match.captured(1).toLongLong();
    }
    QVector<LyricLine> result;
    for (QString line : sourceLines) {
        line = line.trimmed();
        if (line.startsWith(QChar(0xfeff)))
            line.remove(0, 1);
        QVector<qint64> times;
        while (true) {
            const auto match = timestamp.match(line);
            if (!match.hasMatch())
                break;
            const QString fraction = match.captured(3).leftJustified(3, QLatin1Char('0'));
            times.append(qMax<qint64>(0, (match.captured(1).toLongLong() * 60 + match.captured(2).toLongLong()) * 1000 +
                                             fraction.toInt() + offset));
            line.remove(0, match.capturedLength());
        }
        const auto text = line.trimmed();
        // Empty timestamp-only lines mark an instrumental gap. They must not
        // become selectable lyric rows, otherwise the UI scrolls away from
        // the last sung line during the gap.
        if (text.isEmpty() || text.startsWith(QLatin1Char('[')))
            continue;
        for (qint64 time : times)
            result.append({time, text});
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const LyricLine &a, const LyricLine &b) { return a.timestampMs < b.timestampMs; });
    return result;
}

bool LrcParser::looksLikeLrc(const QString &lyrics)
{
    const auto lines = parse(lyrics);
    return std::any_of(lines.cbegin(), lines.cend(), [](const LyricLine &line) { return !line.text.isEmpty(); });
}

QJsonArray LrcParser::toJson(const QVector<LyricLine> &lines)
{
    QJsonArray result;
    for (const auto &line : lines) {
        QJsonObject object{{QStringLiteral("time_ms"), line.timestampMs}, {QStringLiteral("text"), line.text}};
        result.append(object);
    }
    return result;
}

} // namespace nekotune
