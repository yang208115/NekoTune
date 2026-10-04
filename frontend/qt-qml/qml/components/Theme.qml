pragma Singleton
import QtQuick

QtObject {
    id: theme
    readonly property var overrides: typeof ignoreExtensionTheme !== "undefined" && ignoreExtensionTheme ? ({}) : typeof controllers !== "undefined" && controllers.extensions ? controllers.extensions.themeTokens : ({})

    // 3.1 Background & Surface Tokens
    readonly property color bgCanvas: overrides.bgCanvas !== undefined ? overrides.bgCanvas : "#0E0D14"
    readonly property color bgSidebar: overrides.bgSidebar !== undefined ? overrides.bgSidebar : "#121019"
    readonly property color bgSurface: overrides.bgSurface !== undefined ? overrides.bgSurface : "#17141F"
    readonly property color bgRaised: overrides.bgRaised !== undefined ? overrides.bgRaised : "#211C2D"
    readonly property color bgHover: overrides.bgHover !== undefined ? overrides.bgHover : "#2A2338"
    readonly property color bgSelected: overrides.bgSelected !== undefined ? overrides.bgSelected : "#322743"
    readonly property color bgCard: overrides.bgCard !== undefined ? overrides.bgCard : bgSurface
    readonly property color borderSubtle: overrides.borderSubtle !== undefined ? overrides.borderSubtle : "#332C41"
    readonly property color borderControl: overrides.borderControl !== undefined ? overrides.borderControl : "#8D809F"

    // 3.1 Text Tokens
    readonly property color textPrimary: overrides.textPrimary !== undefined ? overrides.textPrimary : "#F5F1FA"
    readonly property color textSecondary: overrides.textSecondary !== undefined ? overrides.textSecondary : "#D7CFE2"
    readonly property color textMuted: overrides.textMuted !== undefined ? overrides.textMuted : "#AAA0B8"
    readonly property color textDisabled: overrides.textDisabled !== undefined ? overrides.textDisabled : "#736A83"
    readonly property color textOnAccent: overrides.textOnAccent !== undefined ? overrides.textOnAccent : "#21172F"

    // 3.2 Accent & Brand Tokens
    readonly property color accentPrimary: overrides.accentPrimary !== undefined ? overrides.accentPrimary : "#CBB8FF"
    readonly property color accentHover: overrides.accentHover !== undefined ? overrides.accentHover : "#DBCDFF"
    readonly property color accentPressed: overrides.accentPressed !== undefined ? overrides.accentPressed : "#B7A0ED"
    readonly property color accentSecondary: overrides.accentSecondary !== undefined ? overrides.accentSecondary : "#E8A9C3"

    // 3.2 Status Tokens
    readonly property color statusSuccess: overrides.statusSuccess !== undefined ? overrides.statusSuccess : "#98D8BC"
    readonly property color statusWarning: overrides.statusWarning !== undefined ? overrides.statusWarning : "#E8C58A"
    readonly property color statusError: overrides.statusError !== undefined ? overrides.statusError : "#FF9BAE"
    readonly property color statusErrorBg: overrides.statusErrorBg !== undefined ? overrides.statusErrorBg : "#38202B"
    readonly property color statusInfo: overrides.statusInfo !== undefined ? overrides.statusInfo : "#A9C8F5"

    // 4. Typography
    readonly property string fontFamily: overrides.fontFamily !== undefined ? overrides.fontFamily : "sans-serif"
    readonly property string fontFamilyMonospace: overrides.fontFamilyMonospace !== undefined ? overrides.fontFamilyMonospace : "Monospace, 'Noto Sans Mono', monospace"
    readonly property int fontTitle: overrides.fontTitle !== undefined ? overrides.fontTitle : 24
    readonly property int fontSongTitle: overrides.fontSongTitle !== undefined ? overrides.fontSongTitle : 22
    readonly property int fontDialogTitle: overrides.fontDialogTitle !== undefined ? overrides.fontDialogTitle : 18
    readonly property int fontBody: overrides.fontBody !== undefined ? overrides.fontBody : 14
    readonly property int fontBodySecondary: overrides.fontBodySecondary !== undefined ? overrides.fontBodySecondary : 13
    readonly property int fontCaption: overrides.fontCaption !== undefined ? overrides.fontCaption : 12
    readonly property int fontLyricsWide: overrides.fontLyricsWide !== undefined ? overrides.fontLyricsWide : 24
    readonly property int fontLyricsCompact: overrides.fontLyricsCompact !== undefined ? overrides.fontLyricsCompact : 20
    readonly property real lineHeightBody: overrides.lineHeightBody !== undefined ? overrides.lineHeightBody : 1.5
    readonly property real lineHeightLyrics: overrides.lineHeightLyrics !== undefined ? overrides.lineHeightLyrics : 1.6

    // 5. Spacing
    readonly property int spaceXs: overrides.spaceXs !== undefined ? overrides.spaceXs : 4
    readonly property int spaceSm: overrides.spaceSm !== undefined ? overrides.spaceSm : 8
    readonly property int spaceMd: overrides.spaceMd !== undefined ? overrides.spaceMd : 12
    readonly property int spaceLg: overrides.spaceLg !== undefined ? overrides.spaceLg : 16
    readonly property int spaceXl: overrides.spaceXl !== undefined ? overrides.spaceXl : 24
    readonly property int space2Xl: overrides.space2Xl !== undefined ? overrides.space2Xl : 32
    readonly property int space3Xl: overrides.space3Xl !== undefined ? overrides.space3Xl : 48

    // 5. Radius
    readonly property int radiusSm: overrides.radiusSm !== undefined ? overrides.radiusSm : 8
    readonly property int radiusMd: overrides.radiusMd !== undefined ? overrides.radiusMd : 12
    readonly property int radiusLg: overrides.radiusLg !== undefined ? overrides.radiusLg : 16
    readonly property int radiusFull: overrides.radiusFull !== undefined ? overrides.radiusFull : 999

    // 5. Key Dimensions
    readonly property int sidebarWidthWide: overrides.sidebarWidthWide !== undefined ? overrides.sidebarWidthWide : 220
    readonly property int sidebarWidthCompact: overrides.sidebarWidthCompact !== undefined ? overrides.sidebarWidthCompact : 180
    readonly property int bottomBarHeight: overrides.bottomBarHeight !== undefined ? overrides.bottomBarHeight : 96
    readonly property int songRowHeight: overrides.songRowHeight !== undefined ? overrides.songRowHeight : 60
    readonly property int songThumbnailSize: overrides.songThumbnailSize !== undefined ? overrides.songThumbnailSize : 40
    readonly property int controlMinHeight: overrides.controlMinHeight !== undefined ? overrides.controlMinHeight : 40
    readonly property int iconButtonSize: overrides.iconButtonSize !== undefined ? overrides.iconButtonSize : 40
    readonly property int iconButtonDenseSize: overrides.iconButtonDenseSize !== undefined ? overrides.iconButtonDenseSize : 32
    readonly property int playButtonSize: overrides.playButtonSize !== undefined ? overrides.playButtonSize : 48
    readonly property int artworkCompact: overrides.artworkCompact !== undefined ? overrides.artworkCompact : 224
    readonly property int artworkWide: overrides.artworkWide !== undefined ? overrides.artworkWide : 280

    // 10. Animation duration (ms) & Accessibility
    readonly property bool reducedMotion: overrides.reducedMotion !== undefined ? overrides.reducedMotion : false
    readonly property int durationFast: overrides.durationFast !== undefined ? overrides.durationFast : 120
    readonly property int durationNormal: overrides.durationNormal !== undefined ? overrides.durationNormal : 180
    readonly property int durationDrawer: overrides.durationDrawer !== undefined ? overrides.durationDrawer : 200
    readonly property int durationSmooth: overrides.durationSmooth !== undefined ? overrides.durationSmooth : 240
    readonly property int durationScroll: overrides.durationScroll !== undefined ? overrides.durationScroll : 280
}
