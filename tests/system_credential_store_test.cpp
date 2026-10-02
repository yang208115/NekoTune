#ifdef NEKOTUNE_LEGACY_SECRET_SERVICE
#include <libsecret/secret.h>
#endif
#include "infrastructure/credentials/credential_store.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>
using namespace nekotune;

class SystemCredentialStoreTest : public QObject {
    Q_OBJECT
  private slots:
    // Payload sizes span small keys and larger serialized account sessions.
    // The byte pattern includes non-text values so encoding loss cannot hide behind ASCII input.
    // Each size uses the same native round-trip contract.
    void roundTripAndIsolation_data() {
        QTest::addColumn<int>("size");
        QTest::newRow("small") << 32;
        QTest::newRow("key-limit") << 4096;
        QTest::newRow("session-limit") << 16384;
    }
    // Identifiers are unique to this test and cleanup is guarded against early assertion returns.
    // The path-shaped identifier is a credential namespace, not a file to create.
    // Reading through a fresh store object checks native persistence rather than local caching.
    // Other identifiers must remain absent even when their payload sizes match.
    // Overwrite and repeated removal cover the lifecycle required by settings dialogs.
    // On Linux the test wrapper supplies an isolated session keyring for these operations.
    void roundTripAndIsolation() {
        QFETCH(int, size);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto id = directory.filePath("credential");
        const auto other = directory.filePath("other");
        auto store = systemCredentialStore();
        QByteArray value(size, '\0');
        for (int i = 0; i < size; ++i)
            value[i] = char(i % 256);
        const auto missing = store->read(id);
        QVERIFY(missing && !missing.value());
        const auto written = store->write(id, value);
        QVERIFY2(written, written ? "" : qPrintable(written.error().message));
        // Always clean up unique test entries, including after a failed assertion.
        const auto cleanup = qScopeGuard([&] { store->remove(id); });
        const auto loaded = systemCredentialStore()->read(id);
        QVERIFY(loaded && loaded.value());
        QCOMPARE(*loaded.value(), value);
        const auto separate = store->read(other);
        QVERIFY(separate && !separate.value());
        QVERIFY(!QFileInfo::exists(id));
        QVERIFY(store->write(id, "replacement"));
        const auto updated = store->read(id);
        QVERIFY(updated && updated.value());
        QCOMPARE(*updated.value(), QByteArray("replacement"));
        QVERIFY(store->remove(id));
        const auto removed = store->read(id);
        QVERIFY(removed && !removed.value());
        QVERIFY(store->remove(id));
    }
#ifdef NEKOTUNE_LEGACY_SECRET_SERVICE
    // This Linux-only fixture creates the previous Secret Service schema explicitly.
    // The modern store must recognize it, verify the new entry and remove the old one.
    // Removing the migrated credential must not expose a surviving legacy entry on the next read.
    // The scoped cleanup also covers assertion failures after the legacy item was created.
    // No real application credential is used as migration input.
    void migratesPreviousSecretServiceEntry() {
        QTemporaryDir directory;
        const auto id = directory.filePath("legacy");
        const auto hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex();
        const SecretSchema schema = {
            "org.nekotune.Credentials", SECRET_SCHEMA_NONE,
            {{"profile-item", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
            0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        GError *error = nullptr;
        QVERIFY(secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT,
            "NekoTune test migration", "legacy-fixture", nullptr, &error,
            "profile-item", hash.constData(), nullptr));
        QVERIFY(!error);
        auto store = systemCredentialStore();
        const auto cleanup = qScopeGuard([&] { store->remove(id); });
        const auto migrated = store->read(id);
        QVERIFY2(migrated, migrated ? "" : qPrintable(migrated.error().message));
        QVERIFY(migrated.value());
        QCOMPARE(*migrated.value(), QByteArray("legacy-fixture"));
        auto *remaining = secret_password_lookup_sync(&schema, nullptr, &error,
                                                       "profile-item", hash.constData(), nullptr);
        const bool gone = remaining == nullptr;
        secret_password_free(remaining);
        QVERIFY(!error);
        QVERIFY(gone);
        const auto restored = systemCredentialStore()->read(id);
        QVERIFY(restored && restored.value());
        QCOMPARE(*restored.value(), QByteArray("legacy-fixture"));
        QVERIFY(store->remove(id));
        const auto cleared = store->read(id);
        QVERIFY(cleared && !cleared.value());
    }
#endif
};
QTEST_GUILESS_MAIN(SystemCredentialStoreTest)
#include "system_credential_store_test.moc"
