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
            return words[key] || key
        }
    }
    QtObject {
        id: fakeClient
        property var account: ({})
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
        function test_errorAndUnconfiguredEntry() {
            fakeClient.account = {configured: false, logged_in: false, busy: false, credential_error: "locked"}
            tryCompare(findChild(panel, "kugouCredentialError"), "visible", true)
            compare(visibleLoginButtons(panel), 1)
            mouseClick(findChild(panel, "kugouAccountButton"))
            tryCompare(findChild(panel, "kugouLoginPopup"), "opened", true)
        }
    }
}
