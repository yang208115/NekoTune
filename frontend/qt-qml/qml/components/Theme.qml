pragma Singleton
import QtQuick

QtObject {
    id: theme

    // 3.1 Background & Surface Tokens
    readonly property color bgCanvas: "#0E0D14"
    readonly property color bgSidebar: "#121019"
    readonly property color bgSurface: "#17141F"
    readonly property color bgRaised: "#211C2D"
    readonly property color bgHover: "#2A2338"
    readonly property color bgSelected: "#322743"
    readonly property color bgCard: bgSurface
    readonly property color borderSubtle: "#332C41"
    readonly property color borderControl: "#8D809F"

    // 3.1 Text Tokens
    readonly property color textPrimary: "#F5F1FA"
    readonly property color textSecondary: "#D7CFE2"
    readonly property color textMuted: "#AAA0B8"
    readonly property color textDisabled: "#736A83"
    readonly property color textOnAccent: "#21172F"

    // 3.2 Accent & Brand Tokens
    readonly property color accentPrimary: "#CBB8FF"
    readonly property color accentHover: "#DBCDFF"
    readonly property color accentPressed: "#B7A0ED"
    readonly property color accentSecondary: "#E8A9C3"

    // 3.2 Status Tokens
    readonly property color statusSuccess: "#98D8BC"
    readonly property color statusWarning: "#E8C58A"
    readonly property color statusError: "#FF9BAE"
    readonly property color statusErrorBg: "#38202B"
    readonly property color statusInfo: "#A9C8F5"

    // 4. Typography
    readonly property string fontFamily: "sans-serif"
    readonly property string fontFamilyMonospace: "Monospace, 'Noto Sans Mono', monospace"
    readonly property int fontTitle: 24
    readonly property int fontSongTitle: 22
    readonly property int fontDialogTitle: 18
    readonly property int fontBody: 14
    readonly property int fontBodySecondary: 13
    readonly property int fontCaption: 12
    readonly property int fontLyricsWide: 24
    readonly property int fontLyricsCompact: 20
    readonly property real lineHeightBody: 1.5
    readonly property real lineHeightLyrics: 1.6

    // 5. Spacing
    readonly property int spaceXs: 4
    readonly property int spaceSm: 8
    readonly property int spaceMd: 12
    readonly property int spaceLg: 16
    readonly property int spaceXl: 24
    readonly property int space2Xl: 32
    readonly property int space3Xl: 48

    // 5. Radius
    readonly property int radiusSm: 8
    readonly property int radiusMd: 12
    readonly property int radiusLg: 16
    readonly property int radiusFull: 999

    // 5. Key Dimensions
    readonly property int sidebarWidthWide: 220
    readonly property int sidebarWidthCompact: 180
    readonly property int bottomBarHeight: 96
    readonly property int songRowHeight: 60
    readonly property int songThumbnailSize: 40
    readonly property int controlMinHeight: 40
    readonly property int iconButtonSize: 40
    readonly property int iconButtonDenseSize: 32
    readonly property int playButtonSize: 48
    readonly property int artworkCompact: 224
    readonly property int artworkWide: 280

    // 10. Animation duration (ms) & Accessibility
    readonly property bool reducedMotion: false
    readonly property int durationFast: 120
    readonly property int durationNormal: 180
    readonly property int durationDrawer: 200
    readonly property int durationSmooth: 240
    readonly property int durationScroll: 280
}
