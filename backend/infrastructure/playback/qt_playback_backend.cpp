#include "infrastructure/playback/qt_playback_backend.h"
#include <QMediaMetaData>
namespace nekotune {
// Translate Qt Multimedia states into stable domain state values.
// Media-status loading is separate from playback-state transitions.
// Only EndOfMedia requests natural navigation through PlayerEngine.
// Decoder errors also go through the application, which owns queue repair.
// No automatic playlist/shuffle policy is implemented inside this adapter.
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
// Prefer the contributing performer tag for the display/search artist.
// AlbumArtist is a fallback when files omit that more specific field.
// Empty tags are legitimate and are supplemented by library metadata.
// Do not rewrite persisted metadata just because decoder tags differ.
AudioMetadata QtPlaybackBackend::metadata() const {
    auto data = m_player.metaData();
    auto artist = data.stringValue(QMediaMetaData::ContributingArtist);
    if (artist.isEmpty())
        artist = data.stringValue(QMediaMetaData::AlbumArtist);
    return {data.stringValue(QMediaMetaData::Title), artist, data.stringValue(QMediaMetaData::AlbumTitle)};
}
} // namespace nekotune
