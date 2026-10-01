#pragma once
#include "domain/repositories.h"
#include "domain/result.h"
#include <QObject>
namespace nekotune {
struct LibrarySong {
    SongMetadata metadata;
    QString path;
    QVector<SongTag> tags;
};
struct LibrarySnapshot {
    QVector<LibrarySong> songs;
    QVector<SongTag> tags;
};
class LibraryService final : public QObject {
    Q_OBJECT
  public:
    LibraryService(ISongRepository &songs, ITagRepository &tags, ITransaction &transaction);
    LibrarySnapshot snapshot() const;
    Result<SongMetadata> metadata(int id) const;
    QVector<SongTag> tagsFor(int id) const;
    Result<SongMetadata> importFile(const ImportedFile &file, const QString &title = {},
                                    const QString &artist = {});
    Result<SongMetadata> update(int id, const MetadataPatch &patch);
    QString availablePath(int songId) const;
  signals:
    void changed();
    void metadataChanged(const nekotune::SongMetadata &metadata);

  private:
    ISongRepository &m_songs;
    ITagRepository &m_tags;
    ITransaction &m_transaction;
};
} // namespace nekotune
