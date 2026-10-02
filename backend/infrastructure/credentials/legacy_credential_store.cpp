// Include GLib before Qt, whose "signals" macro conflicts with GLib headers.
#include <libsecret/secret.h>
#include "infrastructure/credentials/credential_store.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace nekotune {
namespace {
const SecretSchema schema = {
    "org.nekotune.Credentials", SECRET_SCHEMA_NONE,
    {{"profile-item", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
    0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

// Calls run on the backend thread. Bound even a cancelled/locked keyring prompt.
class Deadline {
  public:
    GCancellable *cancel = g_cancellable_new();
    Deadline() : worker([this] {
        std::unique_lock lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(10), [this] { return done; }))
            g_cancellable_cancel(cancel);
    }) {}
    ~Deadline() {
        { std::lock_guard lock(mutex); done = true; }
        condition.notify_one();
        worker.join();
        g_object_unref(cancel);
    }
  private:
    std::mutex mutex;
    std::condition_variable condition;
    bool done = false;
    std::thread worker;
};

QByteArray itemId(const QString &path) {
    return QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toUtf8(),
                                    QCryptographicHash::Sha256).toHex();
}
AppError keyringError(GError *error) {
    if (error)
        g_error_free(error);
    // Do not propagate provider messages that might include secret data.
    return failure(QStringLiteral("System keyring unavailable or locked; unlock it and retry"),
                   ErrorCode::Storage);
}
bool missingService(const GError *error) {
    return error && (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
                     g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER));
}
class SecretServiceStore final : public CredentialStore {
  public:
    Result<std::optional<QByteArray>> read(const QString &id) override {
        Deadline deadline;
        GError *error = nullptr;
        auto *value = secret_password_lookup_sync(&schema, deadline.cancel, &error,
                                                  "profile-item", itemId(id).constData(), nullptr);
        if (error) {
            secret_password_free(value);
            // KWallet-only desktops have no Secret Service entries to migrate.
            if (missingService(error)) {
                g_error_free(error);
                return std::optional<QByteArray>{};
            }
            return keyringError(error);
        }
        std::optional<QByteArray> result;
        if (value)
            result = QByteArray(value);
        secret_password_free(value);
        return result;
    }
    Result<void> write(const QString &, const QByteArray &) override {
        return failure(QStringLiteral("Legacy keyring storage is read-only"), ErrorCode::Storage);
    }
    Result<void> remove(const QString &id) override {
        Deadline deadline;
        GError *error = nullptr;
        secret_password_clear_sync(&schema, deadline.cancel, &error,
                                   "profile-item", itemId(id).constData(), nullptr);
        if (missingService(error)) {
            g_error_free(error);
            return {};
        }
        if (error)
            return keyringError(error);
        return {};
    }
};
} // namespace
std::shared_ptr<CredentialStore> legacyCredentialStore() {
    return std::make_shared<SecretServiceStore>();
}
} // namespace nekotune
