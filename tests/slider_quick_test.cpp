#include "controllers/library_controller.h"
#include "i18n.h"
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

// Expose real frontend models/controllers to component tests without a running backend.
// An unused temporary socket prevents test commands from reaching a user's player session.
// The temporary music root also isolates any preferences written by the QML fixtures.
// Fake transport objects in individual scenes provide deterministic completion events.
// Optional screenshots are artifacts for inspection, not assertions of visual correctness.
class ComponentTestSetup : public QObject {
    Q_OBJECT
  public:
    Q_INVOKABLE QString screenshotPath(const QString &name) const {
        const auto directory = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT_DIR");
        return directory.isEmpty() ? QString() : directory + "/" + name + ".png";
    }
    ComponentTestSetup() {
        qputenv("NEKOTUNE_SOCKET", m_directory.filePath("unused.sock").toUtf8());
        qputenv("NEKOTUNE_HOME", m_directory.filePath("music").toUtf8());
    }
    // Seed the real record models so filtering and selection logic runs as in the app.
    // Changing through a sentinel tag filter forces reconciliation even if the previous filter was empty.
    // Reset search and selection after the new snapshot to avoid state leaking between test cases.
    Q_INVOKABLE void seedLibrary(const QVariantMap &library) {
        m_library.songs()->update(library.value("songs").toList());
        m_library.tags()->update(library.value("tags").toList());
        m_library.setSelectedTagIds({-1});
        m_library.setSelectedTagIds({});
        m_library.setSearchText({});
        m_library.clearSelection();
    }
  public slots:
    // Each QuickTest engine receives the same fixture/controller objects through explicit context names.
    // The translator is engine-owned, so it is destroyed with the engine instead of leaking across it.
    // Both translator names support components with injected and ambient translation dependencies.
    void qmlEngineAvailable(QQmlEngine *engine) {
        engine->rootContext()->setContextProperty("testLibrary", &m_library);
        engine->rootContext()->setContextProperty("testFixtures", this);
        auto *translator = new I18n(engine);
        engine->rootContext()->setContextProperty("testTranslator", translator);
        engine->rootContext()->setContextProperty("i18n", translator);
    }

  private:
    QTemporaryDir m_directory;
    IpcClient m_client;
    LibraryController m_library{m_client};
};

QUICK_TEST_MAIN_WITH_SETUP(slider, ComponentTestSetup)
#include "slider_quick_test.moc"
