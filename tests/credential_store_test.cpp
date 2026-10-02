#include "infrastructure/kugou/kugou_account_session.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
using namespace nekotune;
namespace {
class MemoryStore final : public CredentialStore {
  public:
    QHash<QString, QByteArray> values;
    bool failRead = false, failWrite = false, failRemove = false, loseWrite = false;
    Result<std::optional<QByteArray>> read(const QString &id) override {
        if (failRead)
            return failure("Keyring locked");
        return values.contains(id) ? std::optional<QByteArray>{values[id]} : std::nullopt;
    }
    Result<void> write(const QString &id, const QByteArray &secret) override {
        if (failWrite)
            return failure("Keyring locked");
        if (!loseWrite)
            values[id] = secret;
        return {};
    }
    Result<void> remove(const QString &id) override {
        if (failRemove)
            return failure("Keyring locked");
        values.remove(id);
        return {};
    }
};
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
const QByteArray session = R"({"version":1,"cookies":{"token":"fixture-token","userid":"123","dfid":"device"}})";
}
class CredentialStoreTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        qputenv("NEKOTUNE_HOME", "/tmp/nekotune-credential-test-isolated");
        qunsetenv("KUGOU_ACCOUNT_API_KEY");
        qunsetenv("KUGOU_ACCOUNT_API_KEY_FILE");
    }
    // The memory store isolates migration policy from platform keyring availability.
    // Legacy bytes are removed only after the secure copy can be read back successfully.
    // Recreating the session proves that its saved status is based on secure storage.
    // Clearing the API key must not clear an independently stored account cookie.
    // Both legacy-file removal and usable-secret restoration are checked explicitly.
    void migratesAndRestoresWithoutPlaintext() {
        QTemporaryDir dir;
        auto store = std::make_shared<MemoryStore>();
        const auto keyPath = dir.filePath("key"), sessionPath = dir.filePath("session");
        write(keyPath, "fixture-key");
        write(sessionPath, session);
        KugouAccountSession first(keyPath, sessionPath, store);
        QCOMPARE(first.key, QString("fixture-key"));
        QCOMPARE(first.cookies.value("token"), QString("fixture-token"));
        QVERIFY(first.keySaved);
        QVERIFY(first.keyError.isEmpty() && first.sessionError.isEmpty());
        QVERIFY(!QFile::exists(keyPath) && !QFile::exists(sessionPath));
        KugouAccountSession restored(keyPath, sessionPath, store);
        QCOMPARE(restored.cookies, first.cookies);
        QCOMPARE(restored.key, first.key);
        QVERIFY(restored.clearAccountKey().isEmpty());
        KugouAccountSession cleared(keyPath, sessionPath, store);
        QVERIFY(cleared.key.isEmpty() && !cleared.keySaved);
        QCOMPARE(cleared.cookies, first.cookies);
    }
    // These failures occur at different points of the copy-and-verify sequence.
    // A locked store cannot read; a failed write cannot copy; a lost write fails readback.
    // All three must preserve the original legacy file for a later recovery attempt.
    // The lost-write case catches implementations that trust a successful write result alone.
    void retainsLegacyOnFailure_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("locked") << 0;
        QTest::newRow("write-failed") << 1;
        QTest::newRow("readback-missing") << 2;
    }
    // Failed secure migration must not claim that the legacy credential has been saved.
    // The original plaintext remains available for a retry instead of being silently discarded.
    // After the injected store recovers, migration should succeed through the normal path.
    // Only that successful retry is allowed to remove the legacy source.
    // The test checks recovery as well as the initial failure report.
    void retainsLegacyOnFailure() {
        QFETCH(int, mode);
        QTemporaryDir dir;
        auto store = std::make_shared<MemoryStore>();
        store->failRead = mode == 0;
        store->failWrite = mode == 1;
        store->loseWrite = mode == 2;
        const auto keyPath = dir.filePath("key"), sessionPath = dir.filePath("session");
        write(keyPath, "fixture-key");
        write(sessionPath, session);
        KugouAccountSession failed(keyPath, sessionPath, store);
        QVERIFY(!failed.keyError.isEmpty() && !failed.sessionError.isEmpty());
        QVERIFY(QFile::exists(keyPath) && QFile::exists(sessionPath));
        QVERIFY(!failed.keySaved);
        QVERIFY(failed.cookies.isEmpty());
        store->failRead = store->failWrite = store->loseWrite = false;
        KugouAccountSession retry(keyPath, sessionPath, store);
        QVERIFY(retry.keySaved);
        QCOMPARE(retry.cookies.value("token"), QString("fixture-token"));
        QVERIFY(!QFile::exists(keyPath) && !QFile::exists(sessionPath));
    }
    // A failed native-store operation must not introduce an automatic plaintext fallback.
    // Failed key and session saves therefore leave no newly created credential files.
    // A failed removal must continue reporting the old credential as saved.
    // This prevents the UI from showing a cleared state while the secret remains available.
    // The fake store makes each failure deterministic without modifying a real keyring.
    void failsClosedWithoutCreatingFiles() {
        QTemporaryDir dir;
        auto store = std::make_shared<MemoryStore>();
        KugouAccountSession account(dir.filePath("key"), dir.filePath("session"), store);
        store->failWrite = true;
        QVERIFY(!account.saveAccountKey("secret").isEmpty());
        account.cookies.insert("token", "token");
        QVERIFY(!account.saveSession());
        QVERIFY(!QFile::exists(dir.filePath("key")) && !QFile::exists(dir.filePath("session")));
        QVERIFY(!account.keySaved);
        store->failWrite = false;
        QVERIFY(account.saveAccountKey("secret").isEmpty());
        store->failRemove = true;
        QVERIFY(!account.clearAccountKey().isEmpty());
        QVERIFY(account.keySaved);
        QCOMPARE(account.key, QString("secret"));
    }
    // Malformed legacy data is retained for diagnosis instead of being treated as a usable secret.
    // Control characters in a key and broken session JSON exercise different validation paths.
    // A symlinked legacy credential must not grant ownership of the external target.
    // Rejecting it must leave both the link and target data untouched.
    // These cases protect file ownership during migration, not only input parsing.
    void rejectsMalformedAndSymlinkedFiles() {
        QTemporaryDir dir;
        auto store = std::make_shared<MemoryStore>();
        const auto keyPath = dir.filePath("key"), sessionPath = dir.filePath("session");
        write(keyPath, "bad\nkey");
        write(sessionPath, "{broken");
        KugouAccountSession malformed(keyPath, sessionPath, store);
        QVERIFY(!malformed.keyError.isEmpty() && !malformed.sessionError.isEmpty());
        QVERIFY(store->values.isEmpty());
        QVERIFY(QFile::exists(keyPath) && QFile::exists(sessionPath));
        QVERIFY(QFile::remove(keyPath));
        write(dir.filePath("target"), "private");
        QVERIFY(QFile::link(dir.filePath("target"), keyPath));
        KugouAccountSession linked(keyPath, sessionPath, store);
        QVERIFY(!linked.keyError.isEmpty());
        QVERIFY(QFileInfo(keyPath).isSymLink());
        QVERIFY(QFile::exists(dir.filePath("target")));
    }
    // Saved credentials are scoped to a profile even when an environment fallback is shared.
    // One profile's secure key must not become another profile's saved-key status.
    // An explicitly configured external key file remains caller-owned throughout the test.
    // Clearing the saved key restores the fallback without deleting or migrating that file.
    // This distinguishes available authentication from credentials persisted by the app.
    void isolatesProfilesAndKeepsExternalKeyFile() {
        QTemporaryDir dir;
        auto store = std::make_shared<MemoryStore>();
        const auto external = dir.filePath("external");
        write(external, "external-key");
        qputenv("KUGOU_ACCOUNT_API_KEY_FILE", external.toUtf8());
        KugouAccountSession first(dir.filePath("key-a"), dir.filePath("session-a"), store);
        QVERIFY(first.saveAccountKey("saved-key").isEmpty());
        KugouAccountSession other(dir.filePath("key-b"), dir.filePath("session-b"), store);
        QCOMPARE(other.key, QString("external-key"));
        QVERIFY(!other.keySaved);
        QVERIFY(first.clearAccountKey().isEmpty());
        QCOMPARE(first.key, QString("external-key"));
        QVERIFY(QFile::exists(external));
        qunsetenv("KUGOU_ACCOUNT_API_KEY_FILE");
    }
};
QTEST_GUILESS_MAIN(CredentialStoreTest)
#include "credential_store_test.moc"
