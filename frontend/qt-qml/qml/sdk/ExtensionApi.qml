import QtQuick
import "qrc:/qml/components" as Builtin
QtObject {
    id: api
    property var bridge: typeof extensionBridge !== "undefined" ? extensionBridge : null
    readonly property string ownerId: typeof extensionId !== "undefined" ? extensionId : ""
    readonly property var app: typeof controllers !== "undefined" ? controllers : null
    readonly property var window: typeof hostWindow !== "undefined" ? hostWindow : null
    readonly property var theme: Builtin.Theme
    property var pending: ({})
    signal event(var data)
    function call(method, params) {
        return new Promise((resolve, reject) => {
            if (!api.bridge) { reject(new Error("Extension bridge unavailable")); return }
            const requestId = api.bridge.request(method, params || {})
            api.pending[requestId] = {resolve: resolve, reject: reject}
        })
    }
    function invoke(service, params) { return call("extensions.call", {id: api.ownerId, service: service, params: params || {}}) }
    function t(value) { return api.bridge ? api.bridge.label(value, i18n.language) : String(value) }
    property Connections connection: Connections {
        target: api.bridge
        function onCompleted(requestId, data, error) {
            const handler = api.pending[requestId]
            if (!handler) return
            delete api.pending[requestId]
            if (error) handler.reject(new Error(error)); else handler.resolve(data)
        }
        function onEventReceived(data) { api.event(data) }
    }
    Component.onDestruction: { for (const key in pending) pending[key].reject(new Error("Extension view unloaded")); pending = ({}) }
}
