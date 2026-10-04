.pragma library
// Stable IDs connect navigation state, translation keys and page loaders.
// group distinguishes primary, collection and utility sidebar placement.
// The queue page is the reusable playlist browser, not the live drawer.
// The lyrics page can also be presented over the current browse page.
// Debug pages require explicit runtime enablement before navigation.
// Registered QML sources must also appear in the resource manifest.
var pages = [
    {id: "extensions", title: "extensions", source: "pages/ExtensionsPage.qml", group: "utility"},
    {id: "home", title: "home", source: "pages/HomePage.qml", group: "primary"},
    {id: "library", title: "local_music", source: "pages/LibraryPage.qml", group: "primary"},
    {id: "music_sources", title: "music_sources", source: "pages/MusicSourcesPage.qml", group: "primary"},
    {id: "queue", title: "playlists", source: "pages/QueuePage.qml", group: "collection"},
    {id: "lyrics", title: "now_playing", source: "pages/LyricsPage.qml", group: "playing"},
    {id: "settings", title: "settings", source: "pages/SettingsPage.qml", group: "utility"},
    {id: "lyrics_debug", title: "lyrics_debug_page", source: "pages/LyricsDebugPage.qml", debug: true}
]
