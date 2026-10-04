#include "infrastructure/library/audio_reference.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace nekotune {
Result<AudioReference> readAudioReference(const QString &path) {
    const QFileInfo info(path);
    const auto number = info.dir().dirName();
    bool validNumber = false;
    const auto resourceId = number.toLongLong(&validNumber);
    if (info.isSymLink() || QFileInfo(info.absolutePath()).isSymLink() ||
        !validNumber || resourceId <= 0 ||
        !QRegularExpression("^[0-9]{6,}$").match(number).hasMatch() ||
        info.fileName() != number + ".audio.json")
        return failure("Invalid managed audio reference", ErrorCode::Io);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return failure("Cannot read managed audio reference", ErrorCode::Io);
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    AudioReference reference{object.value("path").toString(), object.value("sha256").toString(),
                             object.value("source_name").toString()};
    if (object.value("version").toInt() != 1 || object.value("type").toString() != "external-audio" ||
        !QDir::isAbsolutePath(reference.path) || reference.path == info.absoluteFilePath() ||
        reference.path.endsWith(".audio.json", Qt::CaseInsensitive) ||
        !QRegularExpression("^[0-9a-f]{64}$").match(reference.hash).hasMatch())
        return failure("Invalid managed audio reference", ErrorCode::Io);
    return reference;
}
bool writeAudioReference(const QString &path, const AudioReference &reference) {
    if (QFileInfo(path).isSymLink() || QFileInfo(QFileInfo(path).absolutePath()).isSymLink())
        return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"type", "external-audio"},
                                               {"path", reference.path}, {"sha256", reference.hash},
                                               {"source_name", reference.sourceName}}).toJson();
    return bytes.size() <= 64 * 1024 && file.open(QIODevice::WriteOnly) &&
           file.write(bytes) == bytes.size() && file.commit();
}
bool createAudioSymlink(const QString &source, const QString &target) {
#ifdef Q_OS_WIN
    const auto nativeSource = QDir::toNativeSeparators(source);
    const auto nativeTarget = QDir::toNativeSeparators(target);
    // Windows QFile::link creates shell shortcuts, which are not decoder-readable audio links.
    constexpr DWORD allowUnprivileged = 0x2;
    if (CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(nativeTarget.utf16()),
                           reinterpret_cast<LPCWSTR>(nativeSource.utf16()), allowUnprivileged))
        return true;
    // Older Windows versions do not recognize the unprivileged-create flag.
    return GetLastError() == ERROR_INVALID_PARAMETER &&
           CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(nativeTarget.utf16()),
                               reinterpret_cast<LPCWSTR>(nativeSource.utf16()), 0);
#else
    return QFile::link(source, target);
#endif
}
} // namespace nekotune
