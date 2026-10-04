#pragma once
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
class ExtensionView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantMap descriptor READ descriptor WRITE setDescriptor NOTIFY descriptorChanged)
    Q_PROPERTY(QObject *controllers MEMBER controllers NOTIFY contextChanged)
    Q_PROPERTY(QObject *translator MEMBER translator NOTIFY contextChanged)
    Q_PROPERTY(QObject *hostWindow MEMBER hostWindow NOTIFY contextChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString error READ error NOTIFY readyChanged)
    Q_PROPERTY(QQuickItem *item READ item NOTIFY readyChanged)
  public:
    explicit ExtensionView(QQuickItem *parent = nullptr);
    ~ExtensionView() override;
    QVariantMap descriptor() const { return m_descriptor; }
    void setDescriptor(const QVariantMap &descriptor);
    bool ready() const { return m_item; }
    QString error() const { return m_error; }
    QQuickItem *item() const { return m_item; }
    QObject *controllers = nullptr;
    QObject *translator = nullptr;
    QObject *hostWindow = nullptr;
  signals:
    void descriptorChanged();
    void contextChanged();
    void readyChanged();

  protected:
    void componentComplete() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

  private:
    void schedule();
    void rebuild();
    QVariantMap m_descriptor;
    QQmlEngine *m_engine = nullptr;
    QPointer<QQuickItem> m_item;
    QString m_error;
    bool m_scheduled = false;
};
void registerExtensionQmlTypes();
