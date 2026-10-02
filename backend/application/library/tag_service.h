#pragma once
#include "domain/repositories.h"
#include "domain/result.h"
#include <QObject>
namespace nekotune {
/// Coordinates global reusable tag management and notifications.
/// Renaming a tag changes its label while retaining assignments by ID.
/// Deleting a tag removes associations through repository constraints.
/// Replacing a song's tag set belongs to the metadata transaction.
/// This service does not apply frontend filtering or playback rules.
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
