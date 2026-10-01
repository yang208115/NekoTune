#pragma once
#include "domain/player_state.h"
#include <QObject>
#include <QUrl>
namespace nekotune {
struct AudioMetadata {
    QString title;
    QString artist;
    QString album;
};
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
