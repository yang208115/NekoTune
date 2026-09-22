import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var queue: []
    property var folders: []
    property int currentFolder: 0
    property int editingFolder: 0
    property int movingId: 0
    property bool movingFolder: false
    property var selectedFolder: ({})
    property Item selectedFolderAnchor: null

    property color panelColor: "#191b1f"
    property color lineColor: "#30343b"
    property color textStrongColor: "#f4f0e8"
    property color textSoftColor: "#c5beb2"
    property color textMutedColor: "#8e938f"
    property color mintColor: "#7adfc6"
    property color amberColor: "#f2c86b"
    property color coralColor: "#f0948e"
    property string playbackState: "stopped"

    signal addRequested()
    signal clearRequested()
    signal playRequested(int queueId)
    signal togglePlayPauseRequested()
    signal removeRequested(int queueId)
    signal organizeRequested(string action, var params)

    readonly property bool isPlaying: playbackState === "playing"

    function folderById(id) {
        for (var index = 0; index < folders.length; ++index)
            if (Number(folders[index].id) === Number(id)) return folders[index]
        return null
    }

    function fileName(path) {
        var value = String(path || "").replace(/\\/g, "/")
        var parts = value.split("/")
        return parts.length ? parts[parts.length - 1] : ""
    }

    function folderTrackCount(id) {
        var count = 0
        for (var index = 0; index < queue.length; ++index)
            if (Number(queue[index].folder_id || 0) === Number(id)) ++count
        return count
    }

    function isDescendant(id, ancestor) {
        var current = Number(id)
        while (current !== 0) {
            if (current === Number(ancestor)) return true
            var folder = folderById(current)
            if (!folder) break
            current = Number(folder.parent_id || 0)
        }
        return false
    }

    function parentFolder() {
        var folder = folderById(currentFolder)
        currentFolder = folder ? Number(folder.parent_id || 0) : 0
    }

    function placePopup(popup, anchor) {
        if (anchor) {
            var point = anchor.mapToItem(Overlay.overlay, 0, 0)
            var preferredX = point.x + anchor.width - popup.width
            var preferredY = point.y + anchor.height + 8
            if (preferredY + popup.height > Overlay.overlay.height - 12)
                preferredY = point.y - popup.height - 8
            popup.x = Math.max(12, Math.min(preferredX, Overlay.overlay.width - popup.width - 12))
            popup.y = Math.max(12, Math.min(preferredY, Overlay.overlay.height - popup.height - 12))
            return
        }
        popup.x = Math.max(12, (Overlay.overlay.width - popup.width) / 2)
        popup.y = Math.max(12, (Overlay.overlay.height - popup.height) / 2)
    }

    function newFolder() {
        editingFolder = 0
        folderName.text = ""
        namePopup.open()
    }

    function renameFolder(folder) {
        editingFolder = Number(folder.id)
        folderName.text = String(folder.title || folder.name || "")
        namePopup.open()
    }

    function moveItem(id, isFolder) {
        movingId = Number(id)
        movingFolder = isFolder
        movePopup.open()
    }

    function openFolderActions(folder, anchor) {
        selectedFolder = folder
        selectedFolderAnchor = anchor
        folderActions.open()
    }

    readonly property var visibleItems: {
        var result = []
        for (var folderIndex = 0; folderIndex < folders.length; ++folderIndex) {
            var folder = folders[folderIndex]
            if (Number(folder.parent_id || 0) === Number(currentFolder))
                result.push({id: Number(folder.id), title: String(folder.name || ""), isFolder: true, trackCount: folderTrackCount(folder.id)})
        }
        for (var trackIndex = 0; trackIndex < queue.length; ++trackIndex) {
            if (Number(queue[trackIndex].folder_id || 0) === Number(currentFolder))
                result.push(queue[trackIndex])
        }
        return result
    }

    readonly property var breadcrumbItems: {
        var result = [{id: 0, label: i18n.text("queue", i18n.language)}]
        var chain = []
        var current = Number(currentFolder)
        while (current !== 0) {
            var folder = folderById(current)
            if (!folder) break
            chain.unshift({id: Number(folder.id), label: String(folder.name || "")})
            current = Number(folder.parent_id || 0)
        }
        return result.concat(chain)
    }

    readonly property var destinations: {
        var result = [{id: 0, label: i18n.text("queue", i18n.language), depth: 0}]
        function append(parentId, depth) {
            for (var index = 0; index < root.folders.length; ++index) {
                var folder = root.folders[index]
                if (Number(folder.parent_id || 0) !== Number(parentId)) continue
                if (root.movingFolder && root.isDescendant(folder.id, root.movingId)) continue
                result.push({id: Number(folder.id), label: String(folder.name || ""), depth: depth})
                append(folder.id, depth + 1)
            }
        }
        append(0, 1)
        return result
    }

    onFoldersChanged: {
        if (currentFolder !== 0 && !folderById(currentFolder)) currentFolder = 0
    }

    radius: 24
    color: panelColor
    border.color: lineColor
    border.width: 1

    component Glyph: Canvas {
        property string kind: "dot"
        property color glyphColor: "#ffffff"
        width: 18
        height: 18
        antialiasing: true

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = glyphColor
            ctx.fillStyle = glyphColor
            ctx.lineWidth = 1.7
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            var w = width, h = height
            if (kind === "folder" || kind === "folder-open") {
                ctx.beginPath(); ctx.moveTo(2, 5); ctx.lineTo(7, 5); ctx.lineTo(9, 7); ctx.lineTo(w - 2, 7); ctx.lineTo(w - 2, h - 3); ctx.lineTo(2, h - 3); ctx.closePath(); ctx.stroke()
                if (kind === "folder-open") { ctx.globalAlpha = 0.45; ctx.beginPath(); ctx.moveTo(3, 9); ctx.lineTo(w - 3, 9); ctx.stroke(); ctx.globalAlpha = 1 }
            } else if (kind === "plus") {
                ctx.beginPath(); ctx.moveTo(w / 2, 3); ctx.lineTo(w / 2, h - 3); ctx.moveTo(3, h / 2); ctx.lineTo(w - 3, h / 2); ctx.stroke()
            } else if (kind === "close") {
                ctx.beginPath(); ctx.moveTo(4, 4); ctx.lineTo(w - 4, h - 4); ctx.moveTo(w - 4, 4); ctx.lineTo(4, h - 4); ctx.stroke()
            } else if (kind === "back") {
                ctx.beginPath(); ctx.moveTo(w - 3, h / 2); ctx.lineTo(4, h / 2); ctx.moveTo(8, 4); ctx.lineTo(4, h / 2); ctx.lineTo(8, h - 4); ctx.stroke()
            } else if (kind === "chevron") {
                ctx.beginPath(); ctx.moveTo(6, 3); ctx.lineTo(11, h / 2); ctx.lineTo(6, h - 3); ctx.stroke()
            } else if (kind === "play") {
                ctx.beginPath(); ctx.moveTo(6, 3); ctx.lineTo(w - 4, h / 2); ctx.lineTo(6, h - 3); ctx.closePath(); ctx.fill()
            } else if (kind === "pause") {
                ctx.fillRect(4, 3, 3, h - 6); ctx.fillRect(w - 7, 3, 3, h - 6)
            } else if (kind === "more") {
                ctx.beginPath(); ctx.arc(4, h / 2, 1.3, 0, Math.PI * 2); ctx.arc(w / 2, h / 2, 1.3, 0, Math.PI * 2); ctx.arc(w - 4, h / 2, 1.3, 0, Math.PI * 2); ctx.fill()
            } else if (kind === "move") {
                ctx.beginPath(); ctx.moveTo(3, 6); ctx.lineTo(9, 6); ctx.lineTo(9, 3); ctx.lineTo(15, 8); ctx.lineTo(9, 13); ctx.lineTo(9, 10); ctx.lineTo(3, 10); ctx.closePath(); ctx.stroke()
            } else if (kind === "edit") {
                ctx.beginPath(); ctx.moveTo(4, 13); ctx.lineTo(5, 10); ctx.lineTo(12, 3); ctx.lineTo(15, 6); ctx.lineTo(8, 13); ctx.closePath(); ctx.stroke(); ctx.beginPath(); ctx.moveTo(11, 4); ctx.lineTo(14, 7); ctx.stroke()
            } else if (kind === "trash") {
                ctx.beginPath(); ctx.moveTo(4, 5); ctx.lineTo(w - 4, 5); ctx.moveTo(7, 5); ctx.lineTo(7, h - 3); ctx.lineTo(w - 7, h - 3); ctx.lineTo(w - 7, 5); ctx.moveTo(6, 3); ctx.lineTo(w - 6, 3); ctx.stroke()
            } else if (kind === "music") {
                ctx.beginPath(); ctx.moveTo(11, 3); ctx.lineTo(11, 12); ctx.moveTo(11, 4); ctx.lineTo(15, 3); ctx.arc(7, 13, 3, 0, Math.PI * 2); ctx.stroke()
            } else {
                ctx.beginPath(); ctx.arc(w / 2, h / 2, 2, 0, Math.PI * 2); ctx.fill()
            }
        }
        onGlyphColorChanged: requestPaint()
        onKindChanged: requestPaint()
    }

    component IconButton: Button {
        id: iconButton
        property string kind: "dot"
        property string tooltipText: ""
        property color fillColor: "#1b242c"
        property color hoverColor: "#26343e"
        property color pressedColor: "#30434f"
        property color borderColor: "#303c46"
        property color glyphColor: root.textSoftColor
        implicitWidth: 36
        implicitHeight: 36
        hoverEnabled: true
        ToolTip.visible: hovered && tooltipText.length > 0
        ToolTip.delay: 450
        ToolTip.text: tooltipText
        contentItem: Glyph { anchors.centerIn: parent; kind: iconButton.kind; glyphColor: iconButton.enabled ? iconButton.glyphColor : "#5a6268" }
        background: Rectangle { radius: 11; color: !iconButton.enabled ? "#171d22" : iconButton.down ? iconButton.pressedColor : iconButton.hovered ? iconButton.hoverColor : iconButton.fillColor; border.color: iconButton.enabled ? iconButton.borderColor : "#242b31"; border.width: 1 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 9
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label { text: i18n.text("queue", i18n.language); color: root.textStrongColor; font.pixelSize: 22; font.weight: Font.Bold }
                Label { text: i18n.countText("tracks", root.queue.length, i18n.language); color: root.textMutedColor; font.pixelSize: 11; font.weight: Font.DemiBold }
            }
            IconButton { kind: "folder"; tooltipText: i18n.text("new_folder", i18n.language); glyphColor: root.amberColor; fillColor: "#2c281c"; hoverColor: "#3b3320"; borderColor: "#51472b"; onClicked: root.newFolder() }
            IconButton { kind: "plus"; tooltipText: i18n.text("add_music", i18n.language); glyphColor: root.mintColor; fillColor: "#17322d"; hoverColor: "#21443b"; borderColor: "#2b5b50"; onClicked: root.addRequested() }
            IconButton { kind: "close"; tooltipText: i18n.text("clear_queue", i18n.language); enabled: root.queue.length > 0; glyphColor: root.coralColor; fillColor: "transparent"; hoverColor: "#38272a"; borderColor: "#343c43"; onClicked: root.clearRequested() }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: root.lineColor; opacity: 0.7 }

        RowLayout {
            Layout.fillWidth: true
            visible: root.currentFolder !== 0
            spacing: 6
            IconButton { kind: "back"; tooltipText: i18n.text("parent_folder", i18n.language); fillColor: "transparent"; borderColor: "transparent"; onClicked: root.parentFolder() }
            Flickable {
                Layout.fillWidth: true; Layout.preferredHeight: 34; clip: true; contentWidth: breadcrumbRow.width; contentHeight: height; flickableDirection: Flickable.HorizontalFlick
                Row {
                    id: breadcrumbRow; height: parent.height; spacing: 4
                    Repeater {
                        model: root.breadcrumbItems
                        delegate: Button {
                            required property var modelData
                            required property int index
                            implicitHeight: 30; leftPadding: 9; rightPadding: 9; hoverEnabled: true
                            text: modelData.label
                            contentItem: Label { text: parent.text; color: index === root.breadcrumbItems.length - 1 ? root.textStrongColor : root.textMutedColor; font.pixelSize: 11; font.weight: Font.DemiBold; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: 9; color: index === root.breadcrumbItems.length - 1 ? "#1b252d" : "transparent"; border.color: index === root.breadcrumbItems.length - 1 ? "#303e48" : "transparent" }
                            onClicked: root.currentFolder = Number(modelData.id)
                        }
                    }
                }
            }
        }

        ListView {
            id: queueList
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8; model: root.visibleItems; boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                id: row
                required property var modelData
                property bool hovered: rowHover.hovered
                property bool currentTrack: !modelData.isFolder && modelData.state === "current"
                property bool showActions: hovered || currentTrack || modelData.isFolder
                width: queueList.width; height: modelData.isFolder ? 68 : 72; radius: 15; clip: true
                color: currentTrack ? "#17312d" : hovered ? "#1b252d" : modelData.isFolder ? "#181f24" : "#161e24"
                border.color: currentTrack ? "#397262" : hovered ? "#354651" : modelData.isFolder ? "#45402c" : "#27323a"
                border.width: 1
                HoverHandler { id: rowHover; cursorShape: modelData.isFolder ? Qt.PointingHandCursor : Qt.ArrowCursor }
                TapHandler { enabled: modelData.isFolder; onTapped: root.currentFolder = Number(modelData.id) }
                Rectangle { visible: row.currentTrack; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 3; color: root.mintColor }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 8; spacing: 10
                    Rectangle {
                        Layout.preferredWidth: modelData.isFolder ? 46 : 42; Layout.preferredHeight: 42; radius: modelData.isFolder ? 13 : 12
                        color: modelData.isFolder ? "#332d1c" : row.currentTrack ? root.mintColor : "#222d36"
                        border.color: modelData.isFolder ? "#554b2b" : "transparent"
                        Glyph { anchors.centerIn: parent; kind: modelData.isFolder ? "folder" : row.currentTrack ? "music" : "music"; glyphColor: modelData.isFolder ? root.amberColor : row.currentTrack ? "#10201b" : root.textSoftColor; width: modelData.isFolder ? 21 : 18; height: modelData.isFolder ? 21 : 18 }
                        Rectangle { visible: !modelData.isFolder && !row.currentTrack; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.rightMargin: -3; anchors.bottomMargin: -3; width: 20; height: 20; radius: 10; color: "#11171c"; border.color: "#33404a"; Label { anchors.centerIn: parent; text: String(Number(modelData.position || 0) + 1); color: root.textMutedColor; font.pixelSize: 9; font.weight: Font.Bold } }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 3
                        Label { Layout.fillWidth: true; text: modelData.title || i18n.text("untitled", i18n.language); color: modelData.isFolder ? root.textStrongColor : row.currentTrack ? root.mintColor : root.textStrongColor; elide: Text.ElideRight; font.pixelSize: 13; font.weight: modelData.isFolder || row.currentTrack ? Font.Bold : Font.DemiBold }
                        Label { Layout.fillWidth: true; text: modelData.isFolder ? i18n.countText("tracks", Number(modelData.trackCount || 0), i18n.language) : (modelData.artist ? String(modelData.artist) + " · " : "") + root.fileName(modelData.path); color: modelData.isFolder ? "#b6a971" : root.textMutedColor; elide: Text.ElideMiddle; font.pixelSize: 10; font.weight: Font.Medium }
                    }
                    Item {
                        Layout.preferredWidth: row.showActions ? (modelData.isFolder ? 38 : 78) : 0; Layout.preferredHeight: 38
                        RowLayout { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 4; visible: row.showActions
                            IconButton { visible: !modelData.isFolder; kind: row.currentTrack && root.isPlaying ? "pause" : "play"; tooltipText: row.currentTrack && root.isPlaying ? i18n.text("pause", i18n.language) : i18n.text("play_this_track", i18n.language); glyphColor: root.mintColor; fillColor: "#1d3933"; hoverColor: "#295047"; borderColor: "#366156"; onClicked: row.currentTrack ? root.togglePlayPauseRequested() : root.playRequested(Number(modelData.id)) }
                            IconButton { id: itemActionButton; kind: modelData.isFolder ? "more" : "move"; tooltipText: i18n.text(modelData.isFolder ? "folder_actions" : "move_to_folder", i18n.language); fillColor: "transparent"; borderColor: "transparent"; onClicked: modelData.isFolder ? root.openFolderActions(modelData, itemActionButton) : root.moveItem(modelData.id, false) }
                            IconButton { visible: !modelData.isFolder; kind: "trash"; tooltipText: i18n.text("remove", i18n.language); glyphColor: root.coralColor; fillColor: "transparent"; borderColor: "transparent"; onClicked: root.removeRequested(Number(modelData.id)) }
                        }
                    }
                }
            }
            Column {
                anchors.centerIn: parent; width: Math.min(parent.width - 36, 250); spacing: 10; visible: root.visibleItems.length === 0
                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 54; height: 54; radius: 17; color: root.currentFolder ? "#2c281b" : "#1a2229"; border.color: root.currentFolder ? "#4c442b" : root.lineColor; Glyph { anchors.centerIn: parent; kind: root.currentFolder ? "folder-open" : "music"; glyphColor: root.currentFolder ? root.amberColor : root.textMutedColor; width: 25; height: 25 } }
                Label { width: parent.width; text: i18n.text(root.currentFolder ? "empty_folder" : "drop_tracks", i18n.language); color: root.textMutedColor; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; font.pixelSize: 12; font.weight: Font.DemiBold }
            }
        }
    }

    Popup {
        id: namePopup; parent: Overlay.overlay; modal: true; focus: true; width: Math.min(340, Overlay.overlay.width - 24); height: 194; padding: 0; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside; onOpened: { root.placePopup(namePopup); folderName.forceActiveFocus() }
        Overlay.modal: Rectangle { color: "#aa080b0f" }
        background: Rectangle { radius: 18; color: "#171e24"; border.color: "#36434d"; border.width: 1 }
        ColumnLayout { anchors.fill: parent; anchors.margins: 20; spacing: 14
            Label { Layout.fillWidth: true; text: i18n.text(root.editingFolder ? "rename_folder" : "new_folder", i18n.language); color: root.textStrongColor; font.pixelSize: 18; font.weight: Font.Bold }
            TextField { id: folderName; Layout.fillWidth: true; Layout.preferredHeight: 42; maximumLength: 128; placeholderText: i18n.text("folder_name", i18n.language); color: root.textStrongColor; placeholderTextColor: root.textMutedColor; selectionColor: root.mintColor; leftPadding: 13; rightPadding: 13; onAccepted: if (text.trim().length > 0) saveButton.clicked(); background: Rectangle { radius: 11; color: "#0f151a"; border.color: folderName.activeFocus ? root.mintColor : root.lineColor; border.width: folderName.activeFocus ? 2 : 1 } }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } TextButton { text: i18n.text("cancel", i18n.language); subtle: true; implicitWidth: 82; onClicked: namePopup.close() } TextButton { id: saveButton; text: i18n.text("save", i18n.language); implicitWidth: 82; enabled: folderName.text.trim().length > 0; onClicked: { root.organizeRequested(root.editingFolder ? "rename" : "create", {id: root.editingFolder, parent_id: root.currentFolder, name: folderName.text.trim()}); namePopup.close() } } }
        }
    }

    Popup {
        id: movePopup; parent: Overlay.overlay; modal: true; focus: true; width: Math.min(350, Overlay.overlay.width - 24); height: Math.min(470, Overlay.overlay.height - 40); padding: 0; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside; onOpened: root.placePopup(movePopup)
        Overlay.modal: Rectangle { color: "#aa080b0f" }
        background: Rectangle { radius: 18; color: "#171e24"; border.color: "#36434d"; border.width: 1 }
        ColumnLayout { anchors.fill: parent; anchors.margins: 18; spacing: 12
            RowLayout { Layout.fillWidth: true; Label { Layout.fillWidth: true; text: i18n.text("move_to_folder", i18n.language); color: root.textStrongColor; font.pixelSize: 18; font.weight: Font.Bold } IconButton { kind: "close"; fillColor: "transparent"; borderColor: "transparent"; onClicked: movePopup.close() } }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.lineColor }
            ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6; model: root.destinations
                delegate: Button { id: destinationButton; required property var modelData; width: ListView.view.width; height: 46; hoverEnabled: true; leftPadding: 12 + Number(modelData.depth || 0) * 15; text: modelData.label; contentItem: RowLayout { spacing: 9; Glyph { kind: Number(modelData.id) === 0 ? "music" : "folder"; glyphColor: Number(modelData.id) === 0 ? root.mintColor : root.amberColor; width: 18; height: 18 } Label { Layout.fillWidth: true; text: destinationButton.text; color: root.textSoftColor; elide: Text.ElideRight; font.pixelSize: 12; font.weight: Font.DemiBold } } background: Rectangle { radius: 11; color: destinationButton.hovered ? "#24313a" : "#12191f"; border.color: destinationButton.hovered ? "#3c4e59" : "#27333c" } onClicked: { root.organizeRequested(root.movingFolder ? "move" : "move_item", {id: root.movingId, parent_id: Number(modelData.id)}); movePopup.close() } }
            }
        }
    }

    Popup {
        id: folderActions; parent: Overlay.overlay; modal: false; focus: true; width: 210; height: 154; padding: 8; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside; onOpened: root.placePopup(folderActions, root.selectedFolderAnchor)
        background: Rectangle { radius: 14; color: "#1a2229"; border.color: "#3a4852"; border.width: 1 }
        ColumnLayout { anchors.fill: parent; spacing: 2
            TextButton { id: renameAction; Layout.fillWidth: true; text: i18n.text("rename_folder", i18n.language); subtle: true; contentItem: RowLayout { spacing: 9; Glyph { kind: "edit"; glyphColor: root.textSoftColor; width: 16; height: 16 } Label { Layout.fillWidth: true; text: renameAction.text; color: renameAction.enabled ? renameAction.labelColor : "#6a7074"; font: renameAction.font; verticalAlignment: Text.AlignVCenter } } onClicked: { folderActions.close(); root.renameFolder(root.selectedFolder) } }
            TextButton { id: moveAction; Layout.fillWidth: true; text: i18n.text("move_to_folder", i18n.language); subtle: true; contentItem: RowLayout { spacing: 9; Glyph { kind: "move"; glyphColor: root.textSoftColor; width: 16; height: 16 } Label { Layout.fillWidth: true; text: moveAction.text; color: moveAction.enabled ? moveAction.labelColor : "#6a7074"; font: moveAction.font; verticalAlignment: Text.AlignVCenter } } onClicked: { folderActions.close(); root.moveItem(root.selectedFolder.id, true) } }
            TextButton { id: deleteAction; Layout.fillWidth: true; text: i18n.text("delete_folder", i18n.language); subtle: true; labelColor: root.coralColor; contentItem: RowLayout { spacing: 9; Glyph { kind: "trash"; glyphColor: root.coralColor; width: 16; height: 16 } Label { Layout.fillWidth: true; text: deleteAction.text; color: deleteAction.enabled ? deleteAction.labelColor : "#6a7074"; font: deleteAction.font; verticalAlignment: Text.AlignVCenter } } onClicked: { folderActions.close(); deletePopup.open() } }
        }
    }

    Popup {
        id: deletePopup; parent: Overlay.overlay; modal: true; focus: true; width: Math.min(340, Overlay.overlay.width - 24); height: 220; padding: 0; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside; onOpened: root.placePopup(deletePopup)
        Overlay.modal: Rectangle { color: "#aa080b0f" }
        background: Rectangle { radius: 18; color: "#171e24"; border.color: "#4a3539"; border.width: 1 }
        ColumnLayout { anchors.fill: parent; anchors.margins: 20; spacing: 12
            Label { Layout.fillWidth: true; text: i18n.text("delete_folder", i18n.language); color: root.textStrongColor; font.pixelSize: 18; font.weight: Font.Bold }
            Label { Layout.fillWidth: true; Layout.fillHeight: true; text: i18n.text("delete_folder_hint", i18n.language); color: root.textMutedColor; wrapMode: Text.WordWrap; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } TextButton { text: i18n.text("cancel", i18n.language); subtle: true; implicitWidth: 82; onClicked: deletePopup.close() } TextButton { text: i18n.text("delete_folder", i18n.language); implicitWidth: 112; fillColor: "#c76f6b"; hoverColor: "#df827b"; pressedColor: "#a95d5a"; borderColor: root.coralColor; labelColor: "#1b1011"; onClicked: { root.organizeRequested("delete", {id: Number(root.selectedFolder.id)}); deletePopup.close() } } }
        }
    }
}
