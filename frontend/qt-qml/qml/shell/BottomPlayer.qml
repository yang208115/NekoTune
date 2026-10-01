import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import "../components"
Rectangle {
    id: bar
    required property var shell
    required property var controller
    required property var translator
    signal editRequested(var song)

    Layout.fillWidth: true
    Layout.preferredHeight: 96
    color: bar.shell.bgSidebar
    border.color: bar.shell.border
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 16

        // Left Section: Mini Cover & Title & Edit Button (220-280px)
        Item {
            Layout.preferredWidth: bar.shell.width < 1200 ? 220 : 280
            Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    radius: 8
                    clip: true
                    color: bar.shell.surface
                    border.color: bar.shell.border
                    border.width: 1

                    Image {
                        anchors.fill: parent
                        source: "qrc:/artwork/default-cover.png"
                        fillMode: Image.PreserveAspectCrop
                        opacity: bar.shell.hasSong ? 1.0 : 0.4
                    }

                    Image {
                        anchors.fill: parent
                        source: bar.shell.hasSong ? (String(bar.shell.song.cover_url || "")
                                || (!bar.shell.lyrics.offline && bar.shell.lyrics.track_id === bar.shell.song.song_hash
                                    ? String((bar.shell.lyrics.document || {}).cover_url || "") : "")) : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        visible: status === Image.Ready
                    }

                    TapHandler {
                        enabled: bar.shell.hasSong
                        onTapped: bar.shell.viewMode = (bar.shell.viewMode === "queue" ? "lyrics" : "queue")
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    Label {
                        Layout.fillWidth: true
                        text: bar.shell.hasSong ? (bar.shell.song.title || bar.translator.text("untitled", bar.translator.language)) : bar.translator.text("no_track_selected", bar.translator.language)
                        color: bar.shell.ink
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: bar.shell.hasSong ? (bar.shell.song.artist || bar.translator.text("artist_author", bar.translator.language)) : bar.translator.text("choose_audio", bar.translator.language)
                        color: bar.shell.muted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                // Edit Button for current song
                IconButton {
                    kind: "edit"
                    tooltipText: bar.translator.text("edit_track_info", bar.translator.language)
                    glyphColor: bar.shell.muted
                    hoverGlyphColor: bar.shell.lavender
                    enabled: bar.shell.hasSong
                    visible: bar.shell.hasSong
                    implicitWidth: 32
                    implicitHeight: 32
                    iconSize: 16
                    onClicked: bar.editRequested(bar.shell.song)
                }
            }
        }

        // Center Section: Standard Media Controls & Generous Progress Slider
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignHCenter
            spacing: 4

            // Media Control Buttons
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 20

                // Previous Track (|◀)
                Button {
                    id: prevBtn
                    implicitWidth: 36
                    implicitHeight: 36
                    hoverEnabled: true
                    enabled: bar.shell.queue.length > 0
                    background: Rectangle {
                        radius: 18
                        color: prevBtn.down ? bar.shell.bgSelected : prevBtn.hovered ? bar.shell.bgHover : "transparent"
                    }
                    contentItem: Item {
                        anchors.centerIn: parent
                        width: 16; height: 14
                        opacity: prevBtn.enabled ? 1.0 : 0.35

                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2.5; height: 13; radius: 1.25
                            color: bar.shell.ink
                        }
                        Shape {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 11; height: 13
                            ShapePath {
                                fillColor: bar.shell.ink
                                strokeColor: bar.shell.ink
                                strokeWidth: 1
                                joinStyle: ShapePath.RoundJoin
                                startX: 11; startY: 0
                                PathLine { x: 0; y: 6.5 }
                                PathLine { x: 11; y: 13 }
                                PathLine { x: 11; y: 0 }
                            }
                        }
                    }
                    onClicked: bar.controller.previous()
                }

                // Main Play/Pause Button (Section 5: 48x48 Circular, Section 3: textOnAccent #21172F)
                Button {
                    id: mainPlayButton
                    hoverEnabled: true
                    implicitWidth: 48
                    implicitHeight: 48
                    enabled: bar.shell.hasSong || bar.shell.queue.length > 0

                    background: Rectangle {
                        id: mainPlayBg
                        radius: 24
                        color: !mainPlayButton.enabled ? "#2A2338"
                               : mainPlayButton.down ? "#B7A0ED"
                               : mainPlayButton.hovered ? "#DBCDFF"
                               : bar.shell.lavender

                        // Focus ring (Section 7.1 & 11)
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -4
                            radius: parent.radius + 4
                            color: "transparent"
                            border.color: bar.shell.lavender
                            border.width: 2
                            visible: mainPlayButton.activeFocus
                        }

                        Behavior on color { ColorAnimation { duration: 120 } }
                    }

                    contentItem: Item {
                        anchors.fill: parent

                        // Pause State (Two perfect rounded bars)
                        Row {
                            anchors.centerIn: parent
                            spacing: 4.5
                            visible: bar.shell.isPlaying

                            Rectangle {
                                width: 3.5
                                height: 16
                                radius: 1.75
                                color: "#21172F"
                            }
                            Rectangle {
                                width: 3.5
                                height: 16
                                radius: 1.75
                                color: "#21172F"
                            }
                        }

                        // Play State (Optically centered solid triangle)
                        Shape {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 1.5
                            width: 14
                            height: 16
                            visible: !bar.shell.isPlaying

                            ShapePath {
                                fillColor: "#21172F"
                                strokeColor: "#21172F"
                                strokeWidth: 1
                                joinStyle: ShapePath.RoundJoin
                                capStyle: ShapePath.RoundCap
                                startX: 0; startY: 0
                                PathLine { x: 14; y: 8 }
                                PathLine { x: 0; y: 16 }
                                PathLine { x: 0; y: 0 }
                            }
                        }
                    }

                    onClicked: bar.controller.togglePlayPause()
                }

                // Next Track (▶|)
                Button {
                    id: nextBtn
                    implicitWidth: 36
                    implicitHeight: 36
                    hoverEnabled: true
                    enabled: bar.shell.queue.length > 0 && bar.shell.currentIndex < bar.shell.queue.length - 1
                    background: Rectangle {
                        radius: 18
                        color: nextBtn.down ? bar.shell.bgSelected : nextBtn.hovered ? bar.shell.bgHover : "transparent"
                    }
                    contentItem: Item {
                        anchors.centerIn: parent
                        width: 16; height: 14
                        opacity: nextBtn.enabled ? 1.0 : 0.35

                        Shape {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 11; height: 13
                            ShapePath {
                                fillColor: bar.shell.ink
                                strokeColor: bar.shell.ink
                                strokeWidth: 1
                                joinStyle: ShapePath.RoundJoin
                                startX: 0; startY: 0
                                PathLine { x: 11; y: 6.5 }
                                PathLine { x: 0; y: 13 }
                                PathLine { x: 0; y: 0 }
                            }
                        }
                        Rectangle {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2.5; height: 13; radius: 1.25
                            color: bar.shell.ink
                        }
                    }
                    onClicked: bar.controller.next()
                }
            }

            // Progress Slider Row (Spacious & Tactile)
            RowLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: 640
                Layout.alignment: Qt.AlignHCenter
                spacing: 12

                Label {
                    text: formatTime(timelineSlider.pressed
                                     ? timelineSlider.valueAt(timelineSlider.position)
                                     : timelineSlider.value, false)
                    color: bar.shell.subtle
                    font.pixelSize: 11
                    font.family: bar.shell.theme.fontFamilyMonospace
                }

                SeekSlider {
                    id: timelineSlider
                    Layout.fillWidth: true
                    duration: bar.shell.duration
                    playbackPosition: bar.shell.position
                    activeColor: bar.shell.lavender
                    baseColor: bar.shell.border
                    onSeekRequested: positionMs => bar.controller.seek(positionMs)
                }

                Label {
                    text: formatTime(bar.shell.duration, true)
                    color: bar.shell.subtle
                    font.pixelSize: 11
                    font.family: bar.shell.theme.fontFamilyMonospace
                }
            }
        }

        // Right Section: Volume & Lyrics Switch (160-220px)
        RowLayout {
            Layout.preferredWidth: bar.shell.width < 1200 ? 180 : 220
            Layout.fillHeight: true
            spacing: 12

            Item { Layout.fillWidth: true }

            IconButton {
                kind: "volume"
                glyphColor: bar.shell.muted
                hoverGlyphColor: bar.shell.lavender
                implicitWidth: 32
                implicitHeight: 32
                iconSize: 18
                onClicked: bar.controller.toggleMute()
            }

            PlayerSlider {
                Layout.preferredWidth: bar.shell.width < 1200 ? 72 : 88
                from: 0
                to: 1
                value: bar.shell.volume
                activeColor: bar.shell.lavender
                baseColor: bar.shell.border
                onMoved: bar.controller.setVolume(value)
            }

            // Toggle Lyrics View Button
            IconButton {
                kind: "lyrics"
                glyphColor: bar.shell.viewMode === "lyrics" ? bar.shell.lavender : bar.shell.muted
                hoverGlyphColor: bar.shell.lavender
                fillColor: bar.shell.viewMode === "lyrics" ? bar.shell.bgSelected : "transparent"
                hoverColor: bar.shell.bgHover
                implicitWidth: 36
                implicitHeight: 36
                iconSize: 18
                onClicked: bar.shell.viewMode = (bar.shell.viewMode === "queue" ? "lyrics" : "queue")
            }
        }
    }
    function formatTime(ms, isDuration) {
        if (isDuration && (!ms || Number(ms) <= 0)) return "--:--"
        const seconds = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }
}
