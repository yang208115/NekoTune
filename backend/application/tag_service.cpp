#include "application/tag_service.h"
namespace nekotune {
Result<int> TagService::create(const QString &name) {
    auto id = m_tags.createTag(name);
    if (!id)
        return failure(m_tags.errorString());
    emit changed();
    return id;
}
Result<void> TagService::rename(int id, const QString &name) {
    if (!m_tags.renameTag(id, name))
        return failure(m_tags.errorString());
    emit changed();
    return {};
}
Result<void> TagService::remove(int id) {
    if (!m_tags.deleteTag(id))
        return failure(m_tags.errorString());
    emit changed();
    return {};
}
} // namespace nekotune
