#include "infrastructure/lyrics/lyrics_storage.h"
#include "domain/lyrics/krc_parser.h"
#include "domain/lyrics/lrc_parser.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
namespace nekotune {
Result<std::optional<LyricsDocument>> LyricsStorage::readLocal(const LyricsQuery &query,
                                                               const QString &path) const {
    if (path.isEmpty())
        return std::optional<LyricsDocument>{};
    const QFileInfo audio(path);
    // Managed sidecars take precedence; consult the original audio directory only when they are absent.
    if (audio.isSymLink() && audio.exists() &&
        !QFileInfo(audio.dir().filePath(audio.completeBaseName() + ".krc")).exists() &&
        !QFileInfo(audio.dir().filePath(audio.completeBaseName() + ".lrc")).exists())
        return readLocal(query, audio.canonicalFilePath());
    QString localKrcPath;
    QString localPath = audio.absoluteDir().filePath(audio.completeBaseName() + QStringLiteral(".lrc"));
    const auto sidecars = audio.absoluteDir().entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto &sidecar : sidecars) {
        if (sidecar.suffix().compare(QStringLiteral("krc"), Qt::CaseInsensitive) == 0 &&
            sidecar.completeBaseName().compare(audio.completeBaseName(), Qt::CaseInsensitive) == 0)
            localKrcPath = sidecar.absoluteFilePath();
        if (sidecar.suffix().compare(QStringLiteral("lrc"), Qt::CaseInsensitive) == 0 &&
            sidecar.completeBaseName().compare(audio.completeBaseName(), Qt::CaseInsensitive) == 0) {
            localPath = sidecar.absoluteFilePath();
        }
    }
    if (!localKrcPath.isEmpty()) {
        // An invalid KRC may fall through to LRC; unusable word timing should not hide valid local lyrics.
        QFile krc(localKrcPath);
        if (krc.open(QIODevice::ReadOnly) && krc.size() <= 4 * 1024 * 1024) {
            const auto decoded = KrcParser::read(krc.readAll());
            if (decoded && !KrcParser::parse(*decoded).isEmpty()) {
                LyricsDocument document;
                document.source = QStringLiteral("local");
                document.krcLyrics = *decoded;
                document.matched = query;
                return std::optional<LyricsDocument>{document};
            }
        }
    }
    QFile local(localPath);
    if (local.exists()) {
        // Presence of a sidecar is authoritative, including malformed or unreadable
        // files.
        if (!local.open(QIODevice::ReadOnly) || local.size() > 2 * 1024 * 1024) {
            return failure(QStringLiteral("local_read"), ErrorCode::Io);
        }
        LyricsDocument document;
        document.source = QStringLiteral("local");
        const QString localText = QString::fromUtf8(local.readAll());
        if (LrcParser::looksLikeLrc(localText))
            document.syncedLyrics = localText;
        else
            document.plainLyrics = localText;
        document.matched = query;
        document.validate();
        if (document.isEmpty())
            return failure(QStringLiteral("invalid_local"));
        return std::optional<LyricsDocument>{document};
    }
    return std::optional<LyricsDocument>{};
}
} // namespace nekotune
