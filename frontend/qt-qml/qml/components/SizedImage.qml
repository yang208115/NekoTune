import QtQuick
import QtQuick.Window

// Use with explicit display geometry so decoding follows the window's physical pixels.
Image {
    property real decodeScale: Screen.devicePixelRatio
    sourceSize: Qt.size(Math.max(1, Math.ceil(width * decodeScale)),
                        Math.max(1, Math.ceil(height * decodeScale)))
}
