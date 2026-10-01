#pragma once
#include "controllers/feature_controller.h"
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
    QVariantMap m_account{{"configured", false},
                          {"busy", false},
                          {"logged_in", false},
                          {"key_saved", false},
                          {"download_active", false}};
};
class SettingsController final : public FeatureController {
    Q_OBJECT
  public:
    using FeatureController::FeatureController;
    Q_INVOKABLE void kugouSaveKey(const QString &key) { send("kugou.save_key", {{"key", key}}); }
    Q_INVOKABLE void kugouClearKey() { send("kugou.clear_key"); }
};
