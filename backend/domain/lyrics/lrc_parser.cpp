#include "domain/lyrics/lrc_parser.h"

#include <QRegularExpression>
#include <algorithm>

namespace nekotune {

QVector<LyricLine> LrcParser::parse(const QString &lyrics) {
    static const QRegularExpression timestamp(
        QStringLiteral(R"(^\[(\d{1,6}):([0-5]\d)(?:[.:](\d{1,3}))?\])"));
    static const QRegularExpression offsetTag(QStringLiteral(R"(^\[offset:([+-]?\d{1,9})\]$)"),
                                              QRegularExpression::CaseInsensitiveOption);
    // Split before interpreting document metadata so a late offset applies to earlier rows too.
    // Only bounded timestamp forms enter the arithmetic used for millisecond positions.
    // Non-timed metadata and plain text do not become seekable lines.
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
        // One text row can carry several timestamps (repeated choruses), all sharing the same text.
        QVector<qint64> times;
        while (true) {
            const auto match = timestamp.match(line);
            if (!match.hasMatch())
                break;
            // Fractions are decimal seconds: .5, .50 and .500 all mean 500 ms.
            const QString fraction = match.captured(3).leftJustified(3, QLatin1Char('0'));
            times.append(qMax<qint64>(
                0, (match.captured(1).toLongLong() * 60 + match.captured(2).toLongLong()) * 1000 +
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
    // Repeated timestamps are retained rather than deduplicated by their numeric time.
    // Their original relative order determines the last eligible line chosen in the UI.
    // Sorting must therefore remain stable even when the file arrives out of order.
    std::stable_sort(result.begin(), result.end(),
                     [](const LyricLine &a, const LyricLine &b) { return a.timestampMs < b.timestampMs; });
    return result;
}

bool LrcParser::looksLikeLrc(const QString &lyrics) {
    // Detection shares the full parser's acceptance rules rather than looking for one bracket.
    // A timestamp-only marker or metadata tag must not classify plain lyrics as synchronized.
    // At least one accepted nonempty timed lyric row is required.
    const auto lines = parse(lyrics);
    return std::any_of(lines.cbegin(), lines.cend(),
                       [](const LyricLine &line) { return !line.text.isEmpty(); });
}

} // namespace nekotune
