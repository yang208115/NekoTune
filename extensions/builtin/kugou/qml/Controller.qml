import QtQuick
import NekoTune 1.0
QtObject {
    id: client
    property var account: ({enabled:false,configured:false,logged_in:false,busy:false,download_active:false})
    signal requestSucceeded(string method)
    signal requestFailed(string method, string reason)
    signal kugouEvent(var payload)
    property ExtensionApi api: ExtensionApi {
        onEvent: function(data) {
            const prefix = "extension.nekotune.kugou."
            if (!String(data.event || "").startsWith(prefix)) return
            const name = data.event.slice(prefix.length)
            if (name === "status") client.account = data.data
            else client.kugouEvent(Object.assign({}, data.data || {}, {event:"kugou." + name}))
        }
    }
    function invoke(service, params) {
        api.invoke(service,params || {}).then(result => {
            if (result.worker_url !== undefined) account=result
            client.requestSucceeded("kugou." + service)
        }).catch(error => client.requestFailed("kugou." + service,String(error)))
    }
    function kugouSearch(keywords,page) { invoke("search",{keywords:keywords,page:page}) }
    function kugouSendCode(mobile) { invoke("send_code",{mobile:mobile}) }
    function kugouLogin(mobile,code) { invoke("login",{mobile:mobile,code:code}) }
    function kugouDownload(hash) { invoke("download",{hash:hash}) }
    function kugouPlay(hash) { invoke("play",{hash:hash}) }
    function kugouCancel() { invoke("cancel") }
    function kugouSaveConfiguration(enabled,url) { invoke("config.set",{enabled:enabled,worker_url:url}) }
    function kugouSaveKey(key) { invoke("save_key",{key:key}) }
    function kugouClearKey() { invoke("clear_key") }
    function importLibraryPath(path) {
        api.call("library.import",{path:path}).then(()=>client.requestSucceeded("library.import")).catch(error=>client.requestFailed("library.import",String(error)))
    }
    Component.onCompleted: invoke("status")
}
