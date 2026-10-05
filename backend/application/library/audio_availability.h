#pragma once
#include <QFileInfo>

namespace nekotune {
inline bool isAvailableAudioFile(const QString &path) {
    // Windows can report a dangling native link as a file and return its target
    // from canonicalFilePath(). Check the resolved target itself as well.
    const auto target = QFileInfo(path).canonicalFilePath();
    return !target.isEmpty() && QFileInfo(target).isFile();
}
} // namespace nekotune
