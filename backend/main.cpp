#include "runtime/backend_runtime.h"
#include <QCoreApplication>
#include <QDebug>
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("NekoTune Backend"));
    app.setOrganizationName(QStringLiteral("NekoTune"));
    nekotune::BackendRuntime runtime;
    if (!runtime.start()) {
        qCritical() << runtime.errorString();
        return 1;
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &runtime, &nekotune::BackendRuntime::stop);
    qInfo() << "NekoTune backend listening on" << runtime.serverName();
    return app.exec();
}
