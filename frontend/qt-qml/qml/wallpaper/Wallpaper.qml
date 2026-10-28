pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Effects
import "../components"

Window {
    id: window
    objectName: "musicWallpaperWindow"
    visible: false
    color: "transparent"
    title: "NekoTune Music Wallpaper"
    required property var manager
    required property var app
    required property var translator
    required property var transport
    required property QtObject wallpaperData
    property bool outputRunning: false
    readonly property var preferences: manager.settings
    readonly property string style: preferences.style || "classic"
    readonly property var playback: app.playback
    readonly property var song: playback.song
    readonly property bool hasSong: Boolean(song.resource_key || song.song_hash)
    readonly property bool connected: transport.connected && manager.dataReady
    readonly property bool updating: visible && outputRunning && connected && hasSong
    readonly property bool playing: updating && playback.state === "playing"
    readonly property bool transparentOverlay: manager.transparentOverlaySupported && preferences.transparent
    // An explicit source wins; an empty value follows the current layout, then appearance.
    readonly property string themeSource: preferences.themeWallpaper || ""
    readonly property var themeDescriptor: !app.theme ? ({})
        : themeSource !== "" ? app.theme.wallpaperSlots[themeSource] || ({})
        : app.theme.uiSlots.wallpaper || ({})
    readonly property string themeStatus: style !== "theme" ? ""
        : app.theme && app.theme.safeMode ? "safe-mode"
        : !themeDescriptor.source ? (themeSource !== "" ? "source-missing" : "missing")
        : themeContent.error !== "" ? "error"
        : themeContent.ready ? "active" : "loading"
    readonly property string themeError: style === "theme" ? themeContent.error : ""
    function t(key) { return translator.text(key, translator.language) }
    function timeLabel(milliseconds) {
        const seconds = Math.floor(Math.max(0, milliseconds) / 1000)
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }

    Item {
        anchors.fill: parent
        clip: true
        Rectangle { anchors.fill: parent; color: Theme.bgCanvas; visible: !window.transparentOverlay }
        Loader {
            id: background
            anchors.fill: parent
            active: window.visible && window.hasSong && window.preferences.coverBackground
            sourceComponent: Item {
                SizedImage {
                    id: cover
                    objectName: "wallpaperBackgroundImage"
                    anchors.fill: parent
                    source: window.song.cover_url || ""
                    decodeScale: window.screen.devicePixelRatio
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectCrop
                    visible: status === Image.Ready && !blur.active
                }
                Loader {
                    id: blur
                    objectName: "wallpaperBlur"
                    anchors.fill: parent
                    active: window.preferences.blur && cover.status === Image.Ready && GraphicsInfo.api !== GraphicsInfo.Software
                    // The source is static artwork. Progress and decorative transforms
                    // are outside this subtree and never invalidate the blur source.
                    sourceComponent: MultiEffect {
                        source: cover
                        blurEnabled: true; blur: 0.75; blurMax: 48
                        autoPaddingEnabled: false
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    visible: cover.status === Image.Ready
                    color: Theme.bgCanvas; opacity: 0.72
                }
            }
        }

        Loader {
            objectName: "wallpaperStyleLoader"
            anchors.fill: parent
            active: window.style !== "theme" || !themeContent.ready
            sourceComponent: window.style === "vinyl" ? vinylStyle : window.style === "lyrics" ? lyricsStyle : classicStyle
        }
        ThemeSlot {
            id: themeContent
            objectName: "wallpaperThemeSlot"
            anchors.fill: parent
            active: window.style === "theme"
            themeDescriptor: window.themeDescriptor
            controllers: window.app
            translator: window.translator
            hostWindow: window
            wallpaperContext: window.wallpaperData
            showErrors: false
            visible: window.style === "theme" && ready
        }
        Component { id: vinylStyle; VinylWallpaper { wallpaper: window } }
        Component { id: lyricsStyle; LyricsWallpaper { wallpaper: window } }
        Component {
            id: classicStyle
            Item {
                Rectangle {
                    id: panel
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 64, 960)
                    height: content.implicitHeight + 64
                    radius: Theme.radiusLg
                    color: Theme.bgSurface
                    border.color: Theme.borderSubtle

                    Rectangle {
                        id: glow
                        z: -1
                        anchors.centerIn: parent
                        width: parent.width + 40; height: parent.height + 40
                        radius: Theme.radiusLg + 20
                        gradient: Gradient {
                            GradientStop { position: 0; color: Theme.accentPrimary }
                            GradientStop { position: 1; color: Theme.accentSecondary }
                        }
                        opacity: 0.06 + pulse * 0.04 * window.preferences.intensity
                        property real pulse: 0
                        SequentialAnimation on pulse {
                            running: window.playing && !Theme.reducedMotion && window.preferences.intensity > 0
                            loops: Animation.Infinite
                            NumberAnimation { from: 0; to: 1; duration: 4000; easing.type: Easing.InOutSine }
                            NumberAnimation { from: 1; to: 0; duration: 4000; easing.type: Easing.InOutSine }
                        }
                    }

                    ColumnLayout {
                        id: content
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: 32
                        spacing: 24
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 32
                            AlbumArt {
                                Layout.preferredWidth: Math.min(240, panel.width * 0.28)
                                Layout.preferredHeight: Layout.preferredWidth
                                maxDimension: 240
                                decodeScale: window.screen.devicePixelRatio
                                hasSong: window.hasSong
                                coverUrl: window.song.cover_url || ""
                                playbackState: window.playback.state
                            }
                            WallpaperTrackInfo {
                                Layout.fillWidth: true
                                wallpaper: window
                            }
                        }
                        CompactLyrics {
                            objectName: "wallpaperLyrics"
                            Layout.fillWidth: true
                            Layout.preferredHeight: window.preferences.nextLine ? 100 : 60
                            visible: window.preferences.lyrics
                            song: window.song; lyrics: window.app.lyrics.current
                            translator: window.translator
                            position: window.playback.position; playbackRate: window.playback.playbackRate
                            playing: window.playing; connected: window.connected
                            running: window.updating && visible
                            animateLineChanges: window.preferences.intensity > 0
                            showNext: window.preferences.nextLine
                            updateInterval: window.preferences.updateHz === 60 ? 16 : 33
                            pixelSize: 26; previewPixelSize: 18
                        }
                    }
                }
            }
        }
    }
}
