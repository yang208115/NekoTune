#include "infrastructure/qt_playback_backend.h"
#include <QMediaMetaData>
namespace nekotune {
QtPlaybackBackend::QtPlaybackBackend() {
    m_audio.setVolume(.8);
    m_player.setAudioOutput(&m_audio);
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, [this](auto state) {
        emit stateChanged(state == QMediaPlayer::PlayingState  ? PlayerState::Playing
                          : state == QMediaPlayer::PausedState ? PlayerState::Paused
                                                               : PlayerState::Stopped);
    });
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](auto status) {
        if (status == QMediaPlayer::EndOfMedia)
            emit ended();
        else if (status == QMediaPlayer::LoadingMedia)
            emit stateChanged(PlayerState::Loading);
        else if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)
            emit metadataChanged();
    });
    connect(&m_player, &QMediaPlayer::positionChanged, this, &IPlaybackBackend::positionChanged);
    connect(&m_player, &QMediaPlayer::durationChanged, this, &IPlaybackBackend::durationChanged);
    connect(&m_player, &QMediaPlayer::metaDataChanged, this, &IPlaybackBackend::metadataChanged);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this] {
        const auto message = m_player.errorString();
        emit failed(message);
    });
}
AudioMetadata QtPlaybackBackend::metadata() const {
    auto data = m_player.metaData();
    auto artist = data.stringValue(QMediaMetaData::ContributingArtist);
    if (artist.isEmpty())
        artist = data.stringValue(QMediaMetaData::AlbumArtist);
    return {data.stringValue(QMediaMetaData::Title), artist, data.stringValue(QMediaMetaData::AlbumTitle)};
}
} // namespace nekotune
