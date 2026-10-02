import QtQuick
import QtQuick.Controls
import QtTest
import "../../frontend/qt-qml/qml/components" as PlayerComponents

Rectangle {
    id: scene
    width: 820
    height: 500
    color: "#17141F"
    QtObject {
        id: fakeTranslator
        property string language: "zh"
        function text(key, language) {
            const words = {
                kugou_music: "酷狗音乐", kugou_login: "登录", account_logged_in: "已登录",
                kugou_search: "搜索", kugou_search_hint: "搜索歌曲、歌手",
                anonymous_search_hint: "可匿名搜索，登录后即可下载歌曲。",
                kugou_login_prompt: "使用手机号和短信验证码登录", kugou_mobile: "手机号",
                kugou_send_code: "发送验证码", kugou_code: "验证码", close: "关闭",
                kugou_key_missing: "请先在设置中配置酷狗密钥", open_settings: "打开设置",
                kugou_keyring_error: "系统密钥环访问或凭据迁移失败，请解锁后重启。"
            }
            return words[key] || testTranslator.text(key, language) || key
        }
    }
    QtObject {
        id: fakeClient
        property var account: ({})
        property var downloads: []
        property var searches: []
        function kugouDownload(hash) { downloads = downloads.concat([hash]) }
        function kugouSearch(query, page) { searches = searches.concat([{query: query, page: page}]) }
        signal kugouEvent(var payload)
        signal requestSucceeded(string method)
        signal requestFailed(string method, string reason)
    }
    Component {
        id: component
        PlayerComponents.KugouPanel {
            x: 24; y: 24; width: scene.width - 48; height: scene.height - 48
            client: fakeClient
            translator: fakeTranslator
        }
    }
    TestCase {
        id: tests
        name: "KugouAccount"
        when: windowShown
        property var panel
        function init() {
            fakeClient.account = {configured: true, logged_in: false, busy: false, credential_error: ""}
            fakeClient.downloads = []; fakeClient.searches = []
            scene.width = 820; scene.height = 500
            panel = createTemporaryObject(component, scene)
            verify(panel !== null)
        }
        function visibleLoginButtons(item) {
            let count = item instanceof Button && item.visible && item.text === "登录" ? 1 : 0
            for (let child of item.children)
                count += visibleLoginButtons(child)
            return count
        }
        function screenshot(name) {
            const path = testFixtures.screenshotPath(name)
            if (path.length > 0) {
                waitForRendering(panel)
                grabImage(scene).save(path)
            }
        }
        // Account access has one entry point across logged-out and logged-in states.
        // Changing authentication updates its label without introducing another login button.
        // The same entry opens the account dialog while logged out.
        function test_singleEntryAcrossLoginStates() {
            waitForRendering(panel)
            compare(visibleLoginButtons(panel), 1)
            const button = findChild(panel, "kugouAccountButton")
            compare(button.text, "登录")
            screenshot("kugou-logged-out")
            mouseClick(button)
            const popup = findChild(panel, "kugouLoginPopup")
            tryCompare(popup, "opened", true)
            popup.close()
            tryCompare(popup, "opened", false)
            fakeClient.account = {configured: true, logged_in: true, busy: false, credential_error: ""}
            compare(button.text, "已登录")
            compare(button.visible, true)
            compare(visibleLoginButtons(panel), 0)
            screenshot("kugou-logged-in")
        }
        // A locked or unconfigured credential store must remain visible to the user.
        // It must not remove the account entry needed to resolve the configuration problem.
        function test_errorAndUnconfiguredEntry() {
            fakeClient.account = {configured: false, logged_in: false, busy: false, credential_error: "locked"}
            tryCompare(findChild(panel, "kugouCredentialError"), "visible", true)
            compare(visibleLoginButtons(panel), 1)
            mouseClick(findChild(panel, "kugouAccountButton"))
            tryCompare(findChild(panel, "kugouLoginPopup"), "opened", true)
        }
        // Search rows distinguish a loaded image, an absent URL and a failed image request.
        // Only Image.Ready may display artwork above the fallback.
        // A failed cover must not leave a broken-image layer covering the placeholder.
        function test_searchCoversAndFallback() {
            fakeClient.kugouEvent({event: "kugou.search_results", page: 1, songs: [
                {hash: "a", title: "STAGE OF SEKAI", artist: "はりー", cover_url: "qrc:/artwork/default-cover.png"},
                {hash: "b", title: "STAGE OF SEKAI", artist: "Leo/need", cover_url: ""},
                {hash: "c", title: "STAGE OF SEKAI", artist: "初音ミク, はりー", cover_url: "qrc:/artwork/missing-kugou-cover.png"}
            ]})
            ignoreWarning(new RegExp(".*Cannot open: qrc:/artwork/missing-kugou-cover.png"))
            const list = findChild(panel, "kugouResultList")
            tryCompare(list, "count", 3)
            tryVerify(() => list.itemAtIndex(2) !== null)
            const loaded = findChild(list.itemAtIndex(0), "trackCover")
            const missing = findChild(list.itemAtIndex(1), "trackCover")
            const broken = findChild(list.itemAtIndex(2), "trackCover")
            tryCompare(loaded, "status", Image.Ready)
            compare(loaded.visible, true)
            compare(String(loaded.source), "qrc:/artwork/default-cover.png")
            compare(String(missing.source), "")
            compare(missing.visible, false)
            tryCompare(broken, "status", Image.Error)
            compare(broken.visible, false)
            screenshot("kugou-search-covers")
        }
        // Wide rows align their artist and album columns with the header.
        // Narrow rows switch to compact metadata while retaining a usable download action.
        // Download clicks must not select the containing row or start playback through Enter.
        // Pending download state disables the action until a completion or cancellation event.
        function test_responsiveColumnsAndDownloadsDoNotSelectRows() {
            scene.width = 1360; scene.height = 700
            panel.submittedQuery = "STAGE OF SEKAI"
            fakeClient.account = {configured: true, logged_in: true, busy: false}
            fakeClient.kugouEvent({event: "kugou.search_results", page: 1, songs: [
                {hash: "a", title: "A very long song title that needs to fit alongside every other column", artist: "Orangestar,初音ミク", album: "A very long album title that must stay within the album column", duration_ms: 180000},
                {hash: "b", title: "Another song", artist: "Singer", album: "Album", duration_ms: 200000}
            ]})
            const list = findChild(panel, "kugouResultList")
            tryVerify(() => list.itemAtIndex(0) !== null)
            const row = list.itemAtIndex(0)
            waitForRendering(panel)
            const album = findChild(row, "kugouResultAlbum")
            const artists = findChild(row, "kugouResultArtists")
            verify(album.visible && artists.visible)
            compare(artists.mapToItem(panel, 0, 0).x, findChild(panel, "kugouArtistColumn").mapToItem(panel, 0, 0).x)
            compare(album.mapToItem(panel, 0, 0).x, findChild(panel, "kugouAlbumColumn").mapToItem(panel, 0, 0).x)
            compare(artists.names, ["Orangestar", "初音ミク"])
            const title = findChild(row, "kugouResultTitle")
            verify(title.width > 0 && title.truncated)
            verify(title.mapToItem(row, title.width, 0).x <= artists.mapToItem(row, 0, 0).x)
            const download = findChild(row, "kugouDownloadButton")
            row.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(fakeClient.downloads.length, 0)
            mouseClick(download)
            compare(fakeClient.downloads, ["a"])
            compare(panel.selectedHash, "")
            verify(panel.downloading)
            verify(!download.enabled)
            fakeClient.kugouEvent({event: "kugou.download_cancelled"})
            mouseClick(row, 140, 36)
            compare(panel.selectedHash, "a")
            scene.width = 820
            waitForRendering(panel)
            verify(!album.visible && !artists.visible)
            const compactArtists = findChild(row, "kugouCompactArtists")
            verify(compactArtists.visible)
            verify(download.mapToItem(row, download.width, 0).x <= row.width)
            verify(title.width > 0)
        }
        // Paging reuses the submitted query rather than an unsent input edit.
        // Pending search disables navigation until the corresponding result arrives.
        // An unauthenticated account may browse results but cannot start a download.
        // An empty result page stops forward navigation and hides the unused column header.
        function test_paginationAndAccountRestrictions() {
            panel.submittedQuery = "Night"
            fakeClient.kugouEvent({event: "kugou.search_results", page: 2, songs: [{hash: "a", title: "Song"}]})
            const list = findChild(panel, "kugouResultList")
            tryVerify(() => list.itemAtIndex(0) !== null)
            const download = findChild(list.itemAtIndex(0), "kugouDownloadButton")
            verify(!download.enabled)
            waitForRendering(panel)
            mouseClick(findChild(panel, "kugouNextPage"))
            compare(fakeClient.searches.length, 1)
            compare(fakeClient.searches[0].query, "Night")
            compare(fakeClient.searches[0].page, 3)
            verify(panel.waiting)
            verify(!findChild(panel, "kugouPreviousPage").enabled)
            fakeClient.kugouEvent({event: "kugou.search_results", page: 3, songs: [{hash: "b", title: "Song"}]})
            waitForRendering(panel)
            mouseClick(findChild(panel, "kugouPreviousPage"))
            compare(fakeClient.searches[1].page, 2)
            fakeClient.kugouEvent({event: "kugou.search_results", page: 2, songs: []})
            verify(!findChild(panel, "kugouNextPage").enabled)
            verify(!findChild(panel, "kugouColumns").visible)
            compare(panel.message, "")
        }
    }
}
