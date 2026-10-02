#pragma once
#include "controllers/ai_controller.h"
#include "controllers/kugou_controller.h"
#include "controllers/library_controller.h"
#include "controllers/lyrics_controller.h"
#include "controllers/playback_controller.h"
#include "controllers/playlist_controller.h"
#include "controllers/queue_controller.h"
#include "controllers/tag_controller.h"
/// Owns stable feature controller objects for the QML engine lifetime.
/// Each feature subscribes only to relevant events and response fields.
/// This separates frequent playback updates from library selection state.
/// Constant object properties let pages bind once to feature identities.
/// Reconnection supplies fresh backend state through the shared transport.
/// Database location is descriptive backend state rather than a local handle.
class AppControllers final : public QObject {
    Q_OBJECT
    Q_PROPERTY(PlaybackController *playback MEMBER playback CONSTANT)
    Q_PROPERTY(QueueController *queue MEMBER queue CONSTANT)
    Q_PROPERTY(PlaylistController *playlists MEMBER playlists CONSTANT)
    Q_PROPERTY(LibraryController *library MEMBER library CONSTANT)
    Q_PROPERTY(TagController *tags MEMBER tags CONSTANT)
    Q_PROPERTY(LyricsController *lyrics MEMBER lyrics CONSTANT)
    Q_PROPERTY(KugouController *kugou MEMBER kugou CONSTANT)
    Q_PROPERTY(SettingsController *settings MEMBER settings CONSTANT)
    Q_PROPERTY(AiController *ai MEMBER ai CONSTANT)
    Q_PROPERTY(QString databasePath READ databasePath NOTIFY databasePathChanged)
  public:
    explicit AppControllers(IpcClient &client);
    QString databasePath() const { return m_databasePath; }
    PlaybackController *playback;
    QueueController *queue;
    PlaylistController *playlists;
    LibraryController *library;
    TagController *tags;
    LyricsController *lyrics;
    KugouController *kugou;
    SettingsController *settings;
    AiController *ai;
  signals:
    void databasePathChanged();

  private:
    QString m_databasePath;
};
