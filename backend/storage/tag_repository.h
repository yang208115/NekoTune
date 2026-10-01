#pragma once
#include "domain/repositories.h"
#include "storage/database_session.h"
#include <QSqlQuery>
namespace nekotune {
class TagRepository final : public ITagRepository {
  public:
    explicit TagRepository(DatabaseSession &session) : m_session(session), m_db(session.database()) {}
    QString errorString() const override { return m_session.errorString(); }
    QVector<SongTag> tags() const override;
    QHash<int, QVector<SongTag>> songTags() const override;
    int createTag(const QString &name) override;
    bool renameTag(int id, const QString &name) override;
    bool deleteTag(int id) override;
    bool replaceSongTags(int songId, const QStringList &names) override;

  private:
    void setError(const QString &message) { m_session.setError(message); }
    DatabaseSession &m_session;
    QSqlDatabase m_db;
};
} // namespace nekotune
