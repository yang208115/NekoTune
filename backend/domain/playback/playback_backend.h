#pragma once
#include "domain/playback/player_state.h"
#include <QObject>
#include <QUrl>
namespace nekotune {
/// These are decoder tags, which can be missing or arrive late.
/// They supplement library metadata for the current playback source.
/// User-edited title/artist override decoder values in the snapshot.
/// Album remains available for disambiguating online lyric versions.
struct AudioMetadata {
    QString title;
    QString artist;
    QString album;
};
/// Decoder-facing operations use milliseconds and normalized volume.
/// Signals report asynchronous state changes of the current source.
/// The backend owns decoding, not queue identity or navigation rules.
/// An empty URL releases the source after queue removal/shutdown.
/// stop() and clearing a source are intentionally separate actions.
/// Metadata and duration can arrive after loading has already begun.
/// The application coalesces them before automatic lyrics matching.
/// Fake implementations exercise navigation without audio hardware.
class IPlaybackBackend : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual void setSource(const QUrl &url) = 0;
    virtual QUrl source() const = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 position) = 0;
    virtual void setVolume(double volume) = 0;
    virtual qint64 position() const = 0;
    virtual qint64 duration() const = 0;
    virtual double volume() const = 0;
    virtual AudioMetadata metadata() const = 0;
  signals:
    void stateChanged(nekotune::PlayerState state);
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void metadataChanged();
    void ended();
    void failed(const QString &message);
};
} // namespace nekotune
