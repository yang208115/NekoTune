#include "kugou/kugou_account_session.h"
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
    void retainsLegacyOnFailure_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("locked") << 0;
        QTest::newRow("write-failed") << 1;
        QTest::newRow("readback-missing") << 2;
    }
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
