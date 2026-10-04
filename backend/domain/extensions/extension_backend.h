#pragma once
#include "domain/result.h"
#include <QObject>
#include <QVariantMap>
#include <functional>

namespace nekotune {
class IExtensionBackend : public QObject {
    Q_OBJECT
  public:
    using Completion = std::function<void(Result<QVariantMap>)>;
    using HostCall = std::function<void(const QString &, const QVariantMap &, Completion)>;
    using QObject::QObject;
    virtual void request(const QString &method, const QVariantMap &params, Completion completion) = 0;
    virtual QVariantMap snapshot() const = 0;
    virtual void publish(const QVariantMap &event) = 0;
  signals:
    void changed();
    void eventReady(const QVariantMap &event);
};
} // namespace nekotune
