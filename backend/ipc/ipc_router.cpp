#include "ipc/ipc_router.h"

namespace nekotune {

IpcRouter::IpcRouter(PlayerEngine &player)
    : m_player(player)
{
}

QJsonObject IpcRouter::dispatch(const QJsonObject &request) const
{
    const auto id = request.value(QStringLiteral("id"));
    const auto method = request.value(QStringLiteral("method")).toString();
    auto error = [&](const QString &message) {
        QJsonObject result{{QStringLiteral("status"), QStringLiteral("error")},
                           {QStringLiteral("message"), message}};
        if (!id.isUndefined()) {
            result.insert(QStringLiteral("id"), id);
        }
        return result;
    };

    if (method.isEmpty()) {
        return error(QStringLiteral("Request method is required"));
    }

    const auto params = request.value(QStringLiteral("params")).toObject();
    QJsonObject result;
    if (method == QStringLiteral("player.play")) {
        result = m_player.play(params);
    } else if (method == QStringLiteral("player.toggle_play_pause")) {
        result = m_player.togglePlayPause();
    } else if (method == QStringLiteral("player.pause")) {
        result = m_player.pause();
    } else if (method == QStringLiteral("player.stop")) {
        result = m_player.stop();
    } else if (method == QStringLiteral("player.next")) {
        result = m_player.next();
    } else if (method == QStringLiteral("player.previous")) {
        result = m_player.previous();
    } else if (method == QStringLiteral("player.seek")) {
        if (!params.value(QStringLiteral("position")).isDouble()) return error(QStringLiteral("position must be a number"));
        result = m_player.seek(static_cast<qint64>(params.value(QStringLiteral("position")).toDouble()));
    } else if (method == QStringLiteral("player.set_volume")) {
        if (!params.value(QStringLiteral("volume")).isDouble()) return error(QStringLiteral("volume must be a number"));
        result = m_player.setVolume(params.value(QStringLiteral("volume")).toDouble());
    } else if (method == QStringLiteral("player.status")) {
        result = {{QStringLiteral("status"), QStringLiteral("ok")},
                  {QStringLiteral("data"), m_player.status()}};
    } else if (method == QStringLiteral("queue.add")) {
        const auto path = params.value(QStringLiteral("path"));
        if (!path.isString() || path.toString().isEmpty()) return error(QStringLiteral("path is required"));
        result = m_player.addToQueue(path.toString());
    } else if (method == QStringLiteral("queue.play") || method == QStringLiteral("queue.remove")) {
        const auto queueId = params.value(QStringLiteral("id"));
        if (!queueId.isDouble()) return error(QStringLiteral("id must be a number"));
        result = method == QStringLiteral("queue.play")
            ? m_player.playQueueItem(queueId.toInt())
            : m_player.removeFromQueue(queueId.toInt());
    } else if (method == QStringLiteral("queue.clear")) {
        result = m_player.clearQueue();
    } else if (method == QStringLiteral("queue.status")) {
        result = {{QStringLiteral("status"), QStringLiteral("ok")},
                  {QStringLiteral("data"), m_player.queueStatus()}};
    } else if (method == QStringLiteral("song.metadata")) {
        result = m_player.songMetadata(params);
    } else if (method == QStringLiteral("song.update_metadata")) {
        result = m_player.updateSongMetadata(params);
    } else {
        return error(QStringLiteral("Unknown method: %1").arg(method));
    }

    if (!id.isUndefined()) {
        result.insert(QStringLiteral("id"), id);
    }
    return result;
}

} // namespace nekotune
