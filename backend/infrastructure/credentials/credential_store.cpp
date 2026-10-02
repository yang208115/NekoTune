#include "infrastructure/credentials/credential_store.h"
#include <qt6keychain/keychain.h>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QSemaphore>
#include <QThread>
#include <atomic>

namespace nekotune {
#ifdef NEKOTUNE_LEGACY_SECRET_SERVICE
std::shared_ptr<CredentialStore> legacyCredentialStore();
#endif
namespace {
const QString service = QStringLiteral("org.nekotune.Credentials");
// Application identifiers are path-shaped to include profile identity.
// Normalize them to absolute paths, then hash them into a stable native-keyring item key.
// The identifier is never opened as a plaintext credential file by this adapter.
QString itemId(const QString &path) {
    return QStringLiteral("nekotune-") + QString::fromLatin1(
        QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toUtf8(),
                                QCryptographicHash::Sha256).toHex());
}
AppError keyringError() {
    return failure(QStringLiteral("System keyring unavailable, busy or locked; unlock it and retry"),
                   ErrorCode::Storage);
}

enum class Operation { Read, Write, Remove };
// The shared request survives the caller's bounded waiting interval.
// A timed-out native job can still write its completion state later.
// Semaphore release publishes the result to the waiting caller.
// abandoned prevents a not-yet-started queued job from proceeding.
// It cannot cancel an already active QtKeychain backend prompt.
struct Request {
    QSemaphore ready;
    std::atomic_bool abandoned = false;
    QKeychain::Error error = QKeychain::OtherError;
    QByteArray data;
};

// QtKeychain has a process-wide job queue. Keep all jobs on one long-lived thread,
// and never run a nested event loop in the player/IPC thread while waiting.
class KeychainRunner {
  public:
    KeychainRunner() {
        context = new QObject;
        context->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, context, &QObject::deleteLater);
        thread.setObjectName(QStringLiteral("NekoTune Keychain"));
        thread.start();
    }
    ~KeychainRunner() {
        thread.quit();
        thread.wait();
    }
    Result<std::optional<QByteArray>> run(Operation operation, const QString &id,
                                        const QByteArray &secret = {}) {
        auto request = std::make_shared<Request>();
        QMetaObject::invokeMethod(context, [this, request, operation, id, secret] {
            if (request->abandoned)
                return;
            // One native job may remain active after its caller's ten-second wait has expired.
            // Reject additional operations instead of queueing behind an unresolved desktop prompt.
            // The default request error turns this early release into a busy/unavailable failure.
            if (active) {
                request->ready.release();
                return;
            }
            QKeychain::Job *job = nullptr;
            if (operation == Operation::Read) {
                job = new QKeychain::ReadPasswordJob(service);
            } else if (operation == Operation::Write) {
                auto *write = new QKeychain::WritePasswordJob(service);
                write->setBinaryData(secret);
                job = write;
            } else {
                job = new QKeychain::DeletePasswordJob(service);
            }
            job->setKey(itemId(id));
            // A locked keyring must fail explicitly rather than silently persisting plaintext settings.
            job->setInsecureFallback(false);
            active = true;
            QObject::connect(job, &QKeychain::Job::finished, context,
                             [this, request, operation](QKeychain::Job *finished) {
                request->error = finished->error();
                if (operation == Operation::Read && request->error == QKeychain::NoError)
                    request->data = static_cast<QKeychain::ReadPasswordJob *>(finished)->binaryData();
                active = false;
                request->ready.release();
            });
            job->start();
        });
        if (!request->ready.tryAcquire(1, 10000)) {
            request->abandoned = true;
            // QtKeychain has no cancellation API. A pending job must remain alive
            // until its callback; further operations fail as busy in the meantime.
            return keyringError();
        }
        // Read and remove are allowed to observe an absent entry.
        // Write must report any native error, even if its error code resembles absence.
        // This keeps idempotent clearing separate from a falsely successful save.
        if (request->error == QKeychain::EntryNotFound && operation != Operation::Write)
            return std::optional<QByteArray>{};
        if (request->error != QKeychain::NoError)
            return keyringError();
        return std::optional<QByteArray>{request->data};
    }
  private:
    QThread thread;
    QObject *context;
    bool active = false;
};
KeychainRunner &runner() {
    static KeychainRunner instance;
    return instance;
}

// Read new-format credentials first before attempting Linux migration.
// An unavailable native store fails instead of pretending the entry is absent.
// Legacy entries are removed only after a verified native write/read pair.
// Explicit remove clears legacy state first to prevent resurrection.
// Returned failures contain application messages rather than secret payloads.
class QtKeychainStore final : public CredentialStore {
  public:
    Result<std::optional<QByteArray>> read(const QString &id) override {
        auto current = runner().run(Operation::Read, id);
        if (!current || current.value())
            return current;
#ifdef NEKOTUNE_LEGACY_SECRET_SERVICE
        auto legacy = legacyCredentialStore();
        auto previous = legacy->read(id);
        if (!previous || !previous.value())
            return previous;
        const auto saved = runner().run(Operation::Write, id, *previous.value());
        if (!saved)
            return saved.error();
        const auto verified = runner().run(Operation::Read, id);
        // Migration may fail after writing; retain the old secret until the new copy reads back identically.
        if (!verified || verified.value() != previous.value())
            return failure(QStringLiteral("Cannot verify keyring migration; original entry retained"),
                           ErrorCode::Storage);
        const auto removed = legacy->remove(id);
        if (!removed)
            return removed.error();
        return verified;
#else
        return current;
#endif
    }
    Result<void> write(const QString &id, const QByteArray &secret) override {
        const auto result = runner().run(Operation::Write, id, secret);
        return result ? Result<void>{} : Result<void>{result.error()};
    }
    Result<void> remove(const QString &id) override {
#ifdef NEKOTUNE_LEGACY_SECRET_SERVICE
        // Remove the old entry first so clearing cannot restore it on next launch.
        const auto old = legacyCredentialStore()->remove(id);
        if (!old)
            return old;
#endif
        const auto result = runner().run(Operation::Remove, id);
        return result ? Result<void>{} : Result<void>{result.error()};
    }
};
} // namespace
std::shared_ptr<CredentialStore> systemCredentialStore() {
    return std::make_shared<QtKeychainStore>();
}
} // namespace nekotune
