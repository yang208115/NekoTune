#include "controllers/library_controller.h"
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

class ComponentTestSetup : public QObject {
    Q_OBJECT
  public:
    ComponentTestSetup() { qputenv("NEKOTUNE_SOCKET", m_directory.filePath("unused.sock").toUtf8()); }
    Q_INVOKABLE void seedLibrary(const QVariantMap &library) {
        m_library.songs()->update(library.value("songs").toList());
        m_library.tags()->update(library.value("tags").toList());
        m_library.setSelectedTagIds({-1});
        m_library.setSelectedTagIds({});
        m_library.clearSelection();
    }
  public slots:
    void qmlEngineAvailable(QQmlEngine *engine) {
        engine->rootContext()->setContextProperty("testLibrary", &m_library);
        engine->rootContext()->setContextProperty("testFixtures", this);
    }

  private:
    QTemporaryDir m_directory;
    IpcClient m_client;
    LibraryController m_library{m_client};
};

QUICK_TEST_MAIN_WITH_SETUP(slider, ComponentTestSetup)
#include "slider_quick_test.moc"
