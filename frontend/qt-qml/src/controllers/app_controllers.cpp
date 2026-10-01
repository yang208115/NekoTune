#include "controllers/app_controllers.h"
AppControllers::AppControllers(IpcClient &client)
    : playback(new PlaybackController(client)), queue(new QueueController(client)),
      playlists(new PlaylistController(client)), library(new LibraryController(client)),
      tags(new TagController(client)), lyrics(new LyricsController(client)),
      kugou(new KugouController(client)), settings(new SettingsController(client)) {
    for (QObject *object : QList<QObject *>{static_cast<QObject *>(playback), queue, playlists, library, tags,
                                            lyrics, kugou, settings})
        object->setParent(this);
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        if (event.value("event") != "server.connected")
            return;
        auto path = event.value("data").toObject().value("database_path").toString();
        if (path != m_databasePath) {
            m_databasePath = path;
            emit databasePathChanged();
        }
    });
}
