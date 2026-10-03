#pragma once

#include <QUrl>

namespace nekotune {
inline QUrl kugouRequestUrl(const QUrl &origin, const QString &route) {
    QUrl url(route);
    // Absolute URLs (such as direct KRC downloads) are not Worker routes.
    if (url.isRelative())
        url.setPath(QStringLiteral("/api/music/kugou/v1") + url.path());
    return origin.resolved(url);
}
} // namespace nekotune
