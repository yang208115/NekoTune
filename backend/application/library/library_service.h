#pragma once
#include "domain/repositories.h"
#include "domain/result.h"
#include <QObject>
namespace nekotune {
struct LibrarySong {
    SongMetadata metadata;
    // Empty when no registered path is currently usable; metadata still remains in the library.
    QString path;
    QVector<SongTag> tags;
};
struct LibrarySnapshot {
    QVector<LibrarySong> songs;
    QVector<SongTag> tags;
};
/// Owns library-only import and metadata editing use cases.
/// It deliberately does not select or enqueue imported songs.
/// CollectionService adds that behavior for explicit collection actions.
/// Snapshots keep unavailable songs so their metadata stays editable.
/// Full metadata lookup includes lyrics omitted from ordinary list rows.
/// Writes emit change signals only after the transaction commits.
class LibraryService final : public QObject {
    Q_OBJECT
  public:
    LibraryService(ISongRepository &songs, ITagRepository &tags, ITransaction &transaction);
    /// Return library rows with the complete tag catalog and currently usable paths.
    /// Unavailable audio remains in the result with an empty resolved path.
    /// This is a library snapshot; it neither selects a track nor reports decoder state.
    LibrarySnapshot snapshot() const;
    /// @param id Persistent library song ID, independent of queue occurrence IDs.
    /// @return Full metadata including custom lyrics, or NotFound for a missing song.
    /// Use this lookup for editing instead of the lightweight IPC list representation.
    Result<SongMetadata> metadata(int id) const;
    QVector<SongTag> tagsFor(int id) const;
    /// @param file A completed inspection result with audio hash and registered source path.
    /// @param title Initial custom title for a newly registered hash.
    /// @param artist Initial artist for a newly registered hash.
    /// @return The existing or newly created metadata after its transaction commits.
    /// Known audio retains user edits; this operation does not append to the queue.
    Result<SongMetadata> importFile(const ImportedFile &file, const QString &title = {},
                                    const QString &artist = {});
    /// Apply a background probe only to an existing matching audio identity.
    /// A positive duration already stored by a newer import wins.
    /// Missing/deleted records are ignored rather than recreated.
    /// This makes delayed scan results safe across intervening edits.
    Result<void> backfillDuration(int id, const QString &hash, qint64 durationMs);
    /// @param id Persistent song to edit; missing songs are rejected before writing.
    /// @param patch Optional field replacements, with present empty values meaning clear.
    /// @return Committed metadata, or an error with the previous values still authoritative.
    /// Tag assignment and text changes share one transaction and notification boundary.
    Result<SongMetadata> update(int id, const MetadataPatch &patch);
    /// Return the first remembered path that currently resolves to a regular file.
    /// An empty result means unavailable audio, not a missing metadata record.
    /// The check is a current observation; later playback still handles filesystem changes.
    QString availablePath(int songId) const;
  signals:
    void changed();
    void durationUpdated(const nekotune::SongMetadata &metadata);
    void metadataChanged(const nekotune::SongMetadata &metadata);

  private:
    ISongRepository &m_songs;
    ITagRepository &m_tags;
    ITransaction &m_transaction;
};
} // namespace nekotune
