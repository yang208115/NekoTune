#pragma once
#include "domain/library/library_types.h"
#include "domain/result.h"
#include <QUrl>
#include <functional>
namespace nekotune {
class ISourceResolver {
  public:
    using Completion = std::function<void(Result<QUrl>)>;
    virtual ~ISourceResolver() = default;
    virtual void resolve(const SongMetadata &, Completion) = 0;
    virtual void release(const QUrl &) = 0;
};
} // namespace nekotune
