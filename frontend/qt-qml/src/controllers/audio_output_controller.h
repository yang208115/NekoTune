#pragma once
#include "controllers/feature_controller.h"
#include <QVariantList>

class AudioOutputController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantMap output READ output NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  public:
    explicit AudioOutputController(IpcClient &client);
    QVariantList devices() const { return m_devices; }
    QVariantMap output() const { return m_output; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    void apply(const QJsonObject &data);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void select(const QString &deviceId, const QString &portId = {});
  signals:
    void changed();
    void busyChanged();
    void errorChanged();
  private:
    void setError(const QString &message);
    QVariantList m_devices;
    QVariantMap m_output;
    bool m_busy = false;
    QString m_error;
};
