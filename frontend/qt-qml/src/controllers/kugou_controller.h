#pragma once
#include "controllers/feature_controller.h"
/// Consumes public account flags and forwards provider outcome events.
/// Progress/stage events are frequent and do not need a status query each.
/// Terminal/account events refresh authoritative status once they settle.
/// The controller never receives the stored admission key or cookies.
/// Saved-audio recovery uses the existing library import command.
class KugouController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(QVariantMap account READ account NOTIFY changed)
  public:
    explicit KugouController(IpcClient &client);
    QVariantMap account() const { return m_account; }
    Q_INVOKABLE void kugouSendCode(const QString &mobile) { send("kugou.send_code", {{"mobile", mobile}}); }
    Q_INVOKABLE void kugouLogin(const QString &mobile, const QString &code) {
        send("kugou.login", {{"mobile", mobile}, {"code", code}});
    }
    Q_INVOKABLE void kugouSearch(const QString &keywords, int page = 1) {
        send("kugou.search", {{"keywords", keywords}, {"page", page}});
    }
    Q_INVOKABLE void kugouDownload(const QString &hash) { send("kugou.download", {{"hash", hash}}); }
    Q_INVOKABLE void kugouCancel() { send("kugou.cancel"); }
    Q_INVOKABLE void importLibraryPath(const QString &path) {
        send("library.import", {{"path", IpcClient::normalizePath(path)}});
    }
  signals:
    void changed();
    void kugouEvent(const QVariantMap &event);

  private:
    void apply(const QJsonObject &data);
    QVariantMap m_account{{"enabled", false},
                          {"worker_url", QString()},
                          {"configured", false},
                          {"busy", false},
                          {"logged_in", false},
                          {"key_saved", false},
                          {"download_active", false}};
};
/// Credential input is write-only through explicit settings operations.
/// The caller clears its password field after submitting the value.
/// Clear removes the stored override; launch configuration may still apply.
/// Operation feedback uses flags and failure text rather than key contents.
class SettingsController final : public FeatureController {
    Q_OBJECT
  public:
    using FeatureController::FeatureController;
    Q_INVOKABLE void kugouSaveConfiguration(bool enabled, const QString &workerUrl) {
        send("kugou.config.set", {{"enabled", enabled}, {"worker_url", workerUrl}});
    }
    Q_INVOKABLE void kugouSaveKey(const QString &key) { send("kugou.save_key", {{"key", key}}); }
    Q_INVOKABLE void kugouClearKey() { send("kugou.clear_key"); }
};
