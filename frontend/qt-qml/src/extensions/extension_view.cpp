#include "extensions/extension_view.h"
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QTimer>
#include <qqml.h>
ExtensionView::ExtensionView(QQuickItem *parent) : QQuickItem(parent) {
    connect(this, &ExtensionView::contextChanged, this, &ExtensionView::schedule);
}
ExtensionView::~ExtensionView() {
    delete m_item;
    delete m_engine;
}
void ExtensionView::setDescriptor(const QVariantMap &descriptor) {
    if (descriptor == m_descriptor)
        return;
    m_descriptor = descriptor;
    emit descriptorChanged();
    schedule();
}
void ExtensionView::componentComplete() {
    QQuickItem::componentComplete();
    schedule();
}
void ExtensionView::schedule() {
    if (!isComponentComplete() || m_scheduled)
        return;
    m_scheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_scheduled = false;
        rebuild();
    });
}
void ExtensionView::geometryChange(const QRectF &now, const QRectF &before) {
    QQuickItem::geometryChange(now, before);
    if (m_item)
        m_item->setSize(now.size());
}
void ExtensionView::rebuild() {
    delete m_item;
    delete m_engine;
    m_engine = nullptr;
    m_error.clear();
    const auto path = m_descriptor.value("source").toString();
    if (path.isEmpty()) {
        emit readyChanged();
        return;
    }
    m_engine = new QQmlEngine(this);
    m_engine->addImportPath("qrc:/qt/qml");
    m_engine->addImportPath(QFileInfo(path).absolutePath());
    auto *context = m_engine->rootContext();
    context->setContextProperty("controllers", controllers);
    context->setContextProperty("i18n", translator);
    context->setContextProperty(
        "extensionBridge", controllers ? controllers->property("extensions").value<QObject *>() : nullptr);
    context->setContextProperty("extensionId", m_descriptor.value("extensionId").toString());
    context->setContextProperty("hostWindow", hostWindow);
    QQmlComponent component(m_engine, QUrl::fromLocalFile(path));
    auto *created = component.isReady() ? component.create() : nullptr;
    m_item = qobject_cast<QQuickItem *>(created);
    if (!m_item) {
        QStringList errors;
        for (const auto &error : component.errors())
            errors.append(error.toString());
        m_error = errors.isEmpty() ? "Extension root must be a QtQuick Item" : errors.join('\n');
        delete created;
    } else {
        m_item->setParent(this);
        m_item->setParentItem(this);
        m_item->setSize(size());
    }
    emit readyChanged();
}
void registerExtensionQmlTypes() {
    static bool registered = false;
    if (!registered) {
        qmlRegisterType<ExtensionView>("NekoTune", 1, 0, "ExtensionView");
        registered = true;
    }
}
