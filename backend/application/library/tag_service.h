#pragma once
#include "domain/repositories.h"
#include "domain/result.h"
#include <QObject>
namespace nekotune {
class TagService final : public QObject {
    Q_OBJECT
  public:
    explicit TagService(ITagRepository &tags) : m_tags(tags) {}
    Result<int> create(const QString &name);
    Result<void> rename(int id, const QString &name);
    Result<void> remove(int id);
  signals:
    void changed();

  private:
    ITagRepository &m_tags;
};
} // namespace nekotune
