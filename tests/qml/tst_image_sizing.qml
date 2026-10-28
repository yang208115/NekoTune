import QtQuick
import QtQuick.Window
import QtTest
import "../../frontend/qt-qml/qml/components" as Components

Item {
    id: scene
    width: 640
    height: 480

    Component {
        id: imageComponent
        Components.SizedImage {
            width: 96; height: 96
            source: "qrc:/artwork/default-cover.png"
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
        }
    }
    Component {
        id: originalComponent
        Image { source: "qrc:/artwork/default-cover.png" }
    }
    Component {
        id: albumComponent
        Components.AlbumArt {
            width: 320; height: 320
            hasSong: true
            coverUrl: "qrc:/artwork/default-cover.png"
        }
    }

    TestCase {
        name: "ImageSizing"
        when: windowShown

        function test_largeSourceIsDecodedAtDisplaySize_data() {
            return [{tag: "1x", ratio: 1}, {tag: "2x", ratio: 2}]
        }
        function test_largeSourceIsDecodedAtDisplaySize(data) {
            const original = createTemporaryObject(originalComponent, scene)
            const image = createTemporaryObject(imageComponent, scene, {decodeScale: data.ratio})
            verify(original && image)
            tryCompare(original, "status", Image.Ready)
            tryCompare(image, "status", Image.Ready)
            verify(original.implicitWidth > 1000)
            verify(image.implicitWidth <= 96 * data.ratio)
            verify(image.implicitHeight <= 96 * data.ratio)
            verify(image.implicitWidth < original.implicitWidth / 4)
            compare(image.sourceSize, Qt.size(96 * data.ratio, 96 * data.ratio))
            compare(image.cache, true)

            image.width = 143.5; image.height = 143.5
            const resized = Math.ceil(143.5 * data.ratio)
            tryCompare(image, "sourceSize", Qt.size(resized, resized))
            tryCompare(image, "status", Image.Ready)
            verify(image.implicitWidth <= resized)
            verify(image.implicitWidth > 96 * data.ratio)

            image.decodeScale = data.ratio === 1 ? 2 : 1
            const rescaled = Math.ceil(143.5 * image.decodeScale)
            tryCompare(image, "sourceSize", Qt.size(rescaled, rescaled))
            tryCompare(image, "status", Image.Ready)
            verify(image.implicitWidth <= rescaled)
        }

        function test_windowDprAndZeroGeometry() {
            const image = createTemporaryObject(imageComponent, scene, {width: 0, height: 0})
            verify(image)
            compare(image.decodeScale, image.Screen.devicePixelRatio)
            tryCompare(image, "status", Image.Ready)
            compare(image.sourceSize, Qt.size(1, 1))
            verify(image.implicitWidth <= 1)
            image.width = 64; image.height = 64
            const size = Math.ceil(64 * image.Screen.devicePixelRatio)
            tryCompare(image, "sourceSize", Qt.size(size, size))
            tryCompare(image, "status", Image.Ready)
            verify(image.implicitWidth <= size)
        }

        function test_albumSourceReplacementAndFallback() {
            const album = createTemporaryObject(albumComponent, scene, {decodeScale: 2})
            verify(album)
            const card = album.children.find(child => child.clip === true)
            verify(card)
            const images = card.children.filter(child => child.sourceSize !== undefined)
            compare(images.length, 2)
            const fallback = images[0], cover = images[1]
            tryCompare(fallback, "status", Image.Ready)
            tryCompare(cover, "status", Image.Ready)
            const size = Math.ceil(card.width * 2)
            compare(cover.sourceSize, Qt.size(size, size))
            compare(fallback.sourceSize, cover.sourceSize)
            verify(cover.visible)

            ignoreWarning(new RegExp(".*Cannot open: qrc:/artwork/missing-sized-cover.png"))
            album.coverUrl = "qrc:/artwork/missing-sized-cover.png"
            tryCompare(cover, "status", Image.Error)
            verify(!cover.visible)
            verify(fallback.visible)
            album.hasSong = false
            tryCompare(cover, "status", Image.Null)
            verify(!fallback.visible)
            album.hasSong = true
            album.coverUrl = "qrc:/artwork/default-cover.png"
            tryCompare(cover, "status", Image.Ready)
            verify(cover.visible)
            compare(cover.sourceSize, fallback.sourceSize)
        }
    }
}
