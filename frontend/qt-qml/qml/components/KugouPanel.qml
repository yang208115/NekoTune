pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var client
    required property var translator
    property var results: []
    property int page: 1
    property bool waiting: false
    property bool downloading: false
    property int received: 0
    property int total: 0
    property string message: ""
    property string pendingImportPath: ""
    readonly property var account: client.status.kugou || ({})
    readonly property bool activeDownload: downloading || Boolean(account.download_active)

    function t(key) { return translator.text(key, translator.language) }

    Connections {
        target: root.client
        function onKugouEvent(payload) {
            const event = String(payload.event || "")
            if (event === "kugou.download_progress") {
                root.received = Number(payload.received || 0)
                root.total = Number(payload.total || 0)
                return
            }
            if (event === "kugou.download_stage") {
                root.message = String(payload.stage) === "cover"
                    ? root.t("kugou_fetching_cover") : root.t("kugou_fetching_lyrics")
                return
            }
            root.waiting = false
            if (event === "kugou.search_results") {
                root.results = payload.songs || []
                root.page = Number(payload.page || 1)
                root.message = root.results.length ? "" : root.t("kugou_no_results")
            } else if (event === "kugou.code_sent") {
                root.message = root.t("kugou_code_sent")
            } else if (event === "kugou.logged_in") {
                codeField.text = ""
                root.message = root.t("kugou_login_success")
            } else if (event === "kugou.download_finished") {
                root.downloading = false
                root.pendingImportPath = ""
                const lyricKey = "kugou_lyric_" + String(payload.lyric_status || "error")
                const coverKey = "kugou_cover_" + String(payload.cover_status || "error")
                root.message = root.t("kugou_download_success") + " " + root.t(lyricKey)
                    + " " + root.t(coverKey) + "\n" + String(payload.path || "")
            } else if (event === "kugou.download_cancelled") {
                root.downloading = false
                root.message = root.t("kugou_cancelled")
            } else if (event === "kugou.operation_failed") {
                root.downloading = false
                root.message = String(payload.message || root.t("error"))
                if (payload.path) {
                    root.pendingImportPath = String(payload.path)
                    root.message += "\n" + root.pendingImportPath
                }
            }
        }
        function onRequestSucceeded(method) {
            if (method !== "library.import" || !root.pendingImportPath) return
            root.pendingImportPath = ""
            root.message = root.t("kugou_import_recovered")
        }
        function onRequestFailed(method, reason) {
            if (method === "library.import" && root.pendingImportPath) {
                root.message = reason
                return
            }
            if (!String(method).startsWith("kugou.")) return
            root.waiting = false
            root.downloading = false
            root.message = reason
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        Label {
            text: root.t("kugou_music")
            color: "#F5F1FA"
            font.pixelSize: 25
            font.weight: Font.DemiBold
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: accountColumn.implicitHeight + 28
            radius: 12
            color: "#211C2D"
            border.color: "#332C41"
            ColumnLayout {
                id: accountColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 10
                Label {
                    text: root.account.logged_in ? root.t("kugou_logged_in")
                        : root.account.configured ? root.t("kugou_login_prompt") : root.t("kugou_key_missing")
                    color: root.account.logged_in ? "#98D8BC" : "#D7CFE2"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    visible: !root.account.logged_in
                    Layout.fillWidth: true
                    spacing: 8
                    TextField {
                        id: mobileField
                        Layout.preferredWidth: 190
                        placeholderText: root.t("kugou_mobile")
                        inputMethodHints: Qt.ImhDigitsOnly
                        maximumLength: 11
                        enabled: !root.waiting && !root.account.busy && root.account.configured
                    }
                    Button {
                        text: root.t("kugou_send_code")
                        enabled: !root.waiting && !root.account.busy && root.account.configured
                        onClicked: {
                            root.waiting = true
                            root.message = ""
                            root.client.kugouSendCode(mobileField.text.trim())
                        }
                    }
                    TextField {
                        id: codeField
                        Layout.preferredWidth: 130
                        placeholderText: root.t("kugou_code")
                        echoMode: TextInput.Password
                        inputMethodHints: Qt.ImhDigitsOnly
                        maximumLength: 8
                        enabled: !root.waiting && !root.account.busy && root.account.configured
                    }
                    Button {
                        text: root.t("kugou_login")
                        enabled: !root.waiting && !root.account.busy && root.account.configured
                        onClicked: {
                            const code = codeField.text.trim()
                            codeField.text = ""
                            root.waiting = true
                            root.message = ""
                            root.client.kugouLogin(mobileField.text.trim(), code)
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: root.t("kugou_search_hint")
                enabled: !root.waiting && !root.account.busy
                onAccepted: searchButton.clicked()
            }
            Button {
                id: searchButton
                text: root.t("kugou_search")
                enabled: !root.waiting && !root.account.busy && searchField.text.trim().length > 0
                onClicked: {
                    root.waiting = true
                    root.message = ""
                    root.client.kugouSearch(searchField.text.trim(), 1)
                }
            }
            Button {
                text: root.t("kugou_previous_page")
                enabled: !root.waiting && !root.account.busy && root.page > 1
                onClicked: { root.waiting = true; root.client.kugouSearch(searchField.text.trim(), root.page - 1) }
            }
            Label { text: String(root.page); color: "#D7CFE2" }
            Button {
                text: root.t("kugou_next_page")
                enabled: !root.waiting && !root.account.busy && root.results.length > 0
                onClicked: { root.waiting = true; root.client.kugouSearch(searchField.text.trim(), root.page + 1) }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.waiting || root.activeDownload || root.message.length > 0
            text: root.message || (root.activeDownload ? root.t("kugou_downloading") : root.t("kugou_waiting"))
            color: "#D7CFE2"
            wrapMode: Text.WordWrap
        }
        Button {
            visible: root.pendingImportPath.length > 0
            text: root.t("kugou_retry_import")
            onClicked: root.client.importLibraryPath(root.pendingImportPath)
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.activeDownload
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: root.total > 0 ? root.total : 1
                value: root.total > 0 ? root.received : 0
                indeterminate: root.total <= 0
            }
            Button { text: root.t("cancel"); onClicked: root.client.kugouCancel() }
        }

        ListView {
            id: resultList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: root.results
            ScrollBar.vertical: ScrollBar { }
            delegate: Rectangle {
                id: songRow
                required property var modelData
                width: resultList.width - 10
                height: 66
                radius: 9
                color: "#211C2D"
                border.color: "#332C41"
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: String(songRow.modelData.title || "") + " — " + String(songRow.modelData.artist || "")
                            color: "#F5F1FA"
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: String(songRow.modelData.album || "") + " · "
                                + Math.floor(Number(songRow.modelData.duration_ms || 0) / 60000) + ":"
                                + ("0" + (Math.floor(Number(songRow.modelData.duration_ms || 0) / 1000) % 60)).slice(-2)
                            color: "#AAA0B8"
                            elide: Text.ElideRight
                        }
                    }
                    Button {
                        text: root.t("kugou_download")
                        enabled: root.account.logged_in && !root.waiting && !root.account.busy
                        onClicked: {
                            root.downloading = true
                            root.received = 0
                            root.total = 0
                            root.message = ""
                            root.client.kugouDownload(String(songRow.modelData.hash))
                        }
                    }
                }
            }
        }
    }
}
