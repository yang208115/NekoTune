pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var client
    required property var translator
    signal settingsRequested()
    function focusSearch() { searchField.forceActiveFocus(); searchField.selectAll() }
    property string submittedQuery: ""
    property string selectedHash: ""
    property var results: []
    property int page: 1
    property bool waiting: false
    property bool downloading: false
    property int received: 0
    property int total: 0
    property string message: ""
    property string pendingImportPath: ""
    readonly property var account: client.account || ({})
    readonly property bool activeDownload: downloading || Boolean(account.download_active)
    readonly property bool compactResults: resultsCard.width - 16 < 900
    readonly property real artistColumnWidth: Math.min(280, (resultsCard.width - 16) * 0.21)
    readonly property real albumColumnWidth: Math.min(320, (resultsCard.width - 16) * 0.24)
    Theme { id: palette }

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
                root.message = ""
            } else if (event === "kugou.code_sent") {
                root.message = root.t("kugou_code_sent")
            } else if (event === "kugou.logged_in") {
                codeField.text = ""
                loginPopup.close()
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

    function downloadSong(hash) {
        root.downloading = true
        root.received = 0
        root.total = 0
        root.message = ""
        root.client.kugouDownload(hash)
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: root.t("kugou_music"); color: "#F5F1FA"; font.pixelSize: 26; font.weight: Font.DemiBold }
            TextButton {
                objectName: "kugouAccountButton"
                text: root.t(root.account.logged_in ? "account_logged_in" : "kugou_login")
                subtle: true
                enabled: !root.waiting && !root.account.busy
                onClicked: loginPopup.open()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            InputField {
                id: searchField; objectName: "kugouSearchField"
                Layout.fillWidth: true
                placeholderText: root.t("kugou_search_hint")
                enabled: !root.waiting && !root.account.busy
                onAccepted: if (searchButton.enabled) searchButton.clicked()
                Accessible.name: placeholderText
            }
            TextButton {
                id: searchButton
                text: root.t("kugou_search")
                enabled: !root.waiting && !root.account.busy && searchField.text.trim().length > 0
                onClicked: {
                    root.waiting = true
                    root.message = ""
                    root.submittedQuery = searchField.text.trim()
                    root.client.kugouSearch(root.submittedQuery, 1)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !root.account.logged_in
            Label { Layout.fillWidth: true; text: root.t("anonymous_search_hint"); color: "#AAA0B8"; font.pixelSize: 12; wrapMode: Text.WordWrap }
        }
        Label {
            objectName: "kugouCredentialError"
            Layout.fillWidth: true
            visible: Boolean(root.account.credential_error)
            text: root.t("kugou_keyring_error")
            color: "#E8A0A8"
            wrapMode: Text.WordWrap
        }
        Rectangle {
            id: resultsCard
            objectName: "kugouResultsCard"
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 14
            color: palette.bgSurface
            border.color: palette.borderSubtle
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 12
                    Label { text: root.t("kugou_search_results"); color: palette.textPrimary; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Label {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        text: root.submittedQuery ? root.t("kugou_results_for").replace("%1", root.submittedQuery) : ""
                        textFormat: Text.PlainText
                        color: palette.textMuted; font.pixelSize: 12; elide: Text.ElideRight
                    }
                    Label {
                        text: root.t("kugou_page_count").replace("%1", root.results.length)
                        color: palette.accentPrimary; font.pixelSize: 12
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: palette.borderSubtle }
                RowLayout {
                    objectName: "kugouColumns"
                    visible: root.results.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 16; Layout.rightMargin: 16
                    Layout.preferredHeight: 38
                    spacing: 14
                    Label { Layout.preferredWidth: 28; text: "#"; color: palette.textMuted; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }
                    Item {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Label { anchors.left: parent.left; anchors.leftMargin: 62; anchors.verticalCenter: parent.verticalCenter; text: root.t("kugou_song_column"); color: palette.textMuted; font.pixelSize: 12 }
                    }
                    Label { objectName: "kugouArtistColumn"; Layout.preferredWidth: root.artistColumnWidth; visible: !root.compactResults; text: root.t("artist_author"); color: palette.textMuted; font.pixelSize: 12 }
                    Label { objectName: "kugouAlbumColumn"; Layout.preferredWidth: root.albumColumnWidth; visible: !root.compactResults; text: root.t("album"); color: palette.textMuted; font.pixelSize: 12 }
                    Label { Layout.preferredWidth: 48; text: root.t("duration"); color: palette.textMuted; font.pixelSize: 12; horizontalAlignment: Text.AlignRight }
                    Label { Layout.preferredWidth: 124; text: root.t("kugou_action_column"); color: palette.textMuted; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }
                }
                ListView {
                    id: resultList
                    objectName: "kugouResultList"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 4
                    model: root.results
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: KugouResultRow {
                        required property var modelData
                        required property int index
                        width: resultList.width
                        song: modelData; rowIndex: index
                        translator: root.translator
                        compact: root.compactResults
                        artistWidth: root.artistColumnWidth
                        albumWidth: root.albumColumnWidth
                        selected: root.selectedHash === String(modelData.hash)
                        downloadEnabled: root.account.logged_in && !root.waiting && !root.account.busy && !root.activeDownload
                        onSelectedRequested: root.selectedHash = String(modelData.hash)
                        onDownloadRequested: root.downloadSong(String(modelData.hash))
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: resultList.count === 0
                        text: root.t(root.waiting ? "kugou_waiting" : root.submittedQuery ? "kugou_no_results" : "kugou_search_hint")
                        color: palette.textMuted; font.pixelSize: 14
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: palette.borderSubtle; visible: root.submittedQuery.length > 0 }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 12
                    visible: root.submittedQuery.length > 0
                    Label { Layout.fillWidth: true; text: root.t("kugou_page_number").replace("%1", root.page); color: palette.textMuted; font.pixelSize: 12 }
                    TextButton { objectName: "kugouPreviousPage"; text: root.t("kugou_previous_page"); implicitHeight: 36; subtle: true; enabled: !root.waiting && !root.account.busy && root.page > 1; onClicked: { root.waiting = true; root.client.kugouSearch(root.submittedQuery, root.page - 1) } }
                    TextButton { objectName: "kugouNextPage"; text: root.t("kugou_next_page"); implicitHeight: 36; subtle: true; enabled: !root.waiting && !root.account.busy && root.results.length > 0; onClicked: { root.waiting = true; root.client.kugouSearch(root.submittedQuery, root.page + 1) } }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: root.waiting || root.activeDownload || root.message.length > 0
            text: root.message || root.t(root.activeDownload ? "kugou_downloading" : "kugou_waiting")
            color: "#D7CFE2"; wrapMode: Text.WordWrap; textFormat: Text.PlainText; font.pixelSize: 12
        }
        TextButton { visible: root.pendingImportPath.length > 0; text: root.t("kugou_retry_import"); enabled: !root.waiting && !root.account.busy; onClicked: root.client.importLibraryPath(root.pendingImportPath) }
        RowLayout {
            Layout.fillWidth: true
            visible: root.activeDownload
            ProgressBar { Layout.fillWidth: true; from: 0; to: root.total > 0 ? root.total : 1; value: root.total > 0 ? root.received : 0; indeterminate: root.total <= 0 }
            TextButton { text: root.t("cancel"); subtle: true; onClicked: root.client.kugouCancel() }
        }
    }
    Popup {
        id: loginPopup
        objectName: "kugouLoginPopup"
        parent: Overlay.overlay; anchors.centerIn: parent
        width: Math.min(460, parent.width - 40)
        padding: 24; modal: true; focus: true
        closePolicy: root.account.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
        onClosed: codeField.text = ""
        background: Rectangle { objectName: "shortcutBlocker"; color: "#211C2D"; radius: 16; border.color: "#332C41" }
        contentItem: ColumnLayout {
            spacing: 16
            Label { text: root.t("kugou_login"); color: "#F5F1FA"; font.pixelSize: 20; font.weight: Font.DemiBold }
            Label {
                Layout.fillWidth: true
                text: root.t(root.account.logged_in ? "kugou_logged_in" : root.account.configured ? "kugou_login_prompt" : "kugou_key_missing")
                color: root.account.logged_in ? "#98D8BC" : "#D7CFE2"; wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !root.account.logged_in && root.account.configured
                InputField { id: mobileField; Layout.fillWidth: true; placeholderText: root.t("kugou_mobile"); inputMethodHints: Qt.ImhDigitsOnly; maximumLength: 11; enabled: !root.waiting && !root.account.busy }
                TextButton {
                    text: root.t("kugou_send_code")
                    enabled: !root.waiting && !root.account.busy && mobileField.text.trim().length === 11
                    onClicked: { root.waiting = true; root.message = ""; root.client.kugouSendCode(mobileField.text.trim()) }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !root.account.logged_in && root.account.configured
                InputField { id: codeField; Layout.fillWidth: true; placeholderText: root.t("kugou_code"); echoMode: TextInput.Password; inputMethodHints: Qt.ImhDigitsOnly; maximumLength: 8; enabled: !root.waiting && !root.account.busy }
                TextButton {
                    text: root.t("kugou_login")
                    enabled: !root.waiting && !root.account.busy && mobileField.text.trim().length === 11 && codeField.text.trim().length > 0
                    onClicked: {
                        const code = codeField.text.trim()
                        codeField.text = ""
                        root.waiting = true; root.message = ""
                        root.client.kugouLogin(mobileField.text.trim(), code)
                    }
                }
            }
            TextButton { visible: !root.account.configured; text: root.t("open_settings"); onClicked: { loginPopup.close(); root.settingsRequested() } }
            Label { Layout.fillWidth: true; visible: root.message.length > 0; text: root.message; color: "#D7CFE2"; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
            TextButton { Layout.alignment: Qt.AlignRight; text: root.t("close"); subtle: true; enabled: !root.account.busy; onClicked: loginPopup.close() }
        }
    }
}
