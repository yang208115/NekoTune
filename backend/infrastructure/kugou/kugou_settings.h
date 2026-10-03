#pragma once
#include "app_paths.h"
#include <QJsonObject>
#include <QUrl>

namespace nekotune {
struct KugouSettings {
    bool enabled = false;
    QUrl workerUrl;

    static QUrl migrateWorkerUrl(QUrl url) {
        if (url == QUrl(QStringLiteral("https://kugou-lyrics-api.lyuy.workers.dev")) ||
            url == QUrl(QStringLiteral("https://kugou-lyrics-api.lyuy.workers.dev/")))
            url.setHost(QStringLiteral("luy-music-api.lyuy.workers.dev"));
        return url;
    }

    static KugouSettings load() {
        const auto object = AppPaths::setting("kugou").toObject();
        return {object.value("enabled").toBool(false),
                migrateWorkerUrl(QUrl(object.value("worker_url").toString(), QUrl::StrictMode))};
    }

    QString validationError() const {
        if (workerUrl.isEmpty())
            return enabled ? QStringLiteral("Set the Kugou Worker URL in Settings") : QString();
        const auto host = workerUrl.host().toLower();
        const bool loopback = host == "localhost" || host == "127.0.0.1" || host == "::1";
        if (!workerUrl.isValid() || host.isEmpty() || !workerUrl.userInfo().isEmpty() ||
            workerUrl.hasQuery() || workerUrl.hasFragment() ||
            (!workerUrl.path().isEmpty() && workerUrl.path() != "/") ||
            !(workerUrl.scheme() == "https" || (loopback && workerUrl.scheme() == "http")))
            return QStringLiteral("Invalid Kugou Worker URL: use an HTTPS origin");
        return {};
    }

    QString unavailableReason() const {
        return enabled ? validationError() : QStringLiteral("Enable Kugou music in Settings first");
    }
};
} // namespace nekotune
