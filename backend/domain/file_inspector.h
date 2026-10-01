#pragma once
#include "domain/library_types.h"
#include "domain/result.h"
#include <QObject>
#include <functional>
namespace nekotune {
class IFileInspector : public QObject {
    Q_OBJECT
  public:
    using Completion = std::function<void(Result<ImportedFile>)>;
    using QObject::QObject;
    virtual void inspect(const QString &path, Completion completion) = 0;
    virtual void shutdown() = 0;
};
} // namespace nekotune
