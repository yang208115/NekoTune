#pragma once
#include "domain/library/library_types.h"
#include "domain/result.h"
#include <QObject>
#include <functional>
namespace nekotune {
/// Inspection validates a path and returns an audio identity snapshot.
/// It may perform hashing and duration probing asynchronously.
/// The completion receives either ImportedFile or a typed failure.
/// The production executor returns callbacks to its owning thread.
/// Application services can then safely use their repositories.
/// Shutdown resolves registered inspection callbacks as cancelled.
/// Discovery has a separate lifecycle from these inspection requests.
class IFileInspector : public QObject {
    Q_OBJECT
  public:
    using Completion = std::function<void(Result<ImportedFile>)>;
    using QObject::QObject;
    virtual void inspect(const QString &path, Completion completion) = 0;
    virtual void shutdown() = 0;
};
} // namespace nekotune
