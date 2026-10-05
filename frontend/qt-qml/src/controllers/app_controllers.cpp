#include "controllers/app_controllers.h"
#include "extensions/extension_view.h"
AppControllers::AppControllers(IpcClient &client)
    : playback(new PlaybackController(client)), audioOutput(new AudioOutputController(client)), queue(new QueueController(client)),
      playlists(new PlaylistController(client)), library(new LibraryController(client)),
      tags(new TagController(client)), lyrics(new LyricsController(client)),
      settings(new SettingsController(client)),
      ai(new AiController(client)), extensions(new ExtensionsController(client)) {
    registerExtensionQmlTypes();
    for (QObject *object : QList<QObject *>{static_cast<QObject *>(playback), audioOutput, queue, playlists, library, tags,
                                            lyrics, settings, ai, extensions})
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
