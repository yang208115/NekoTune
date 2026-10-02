#pragma once

#include "domain/lyrics/lyrics_types.h"

#include <QString>

#include <optional>

namespace nekotune {

/// Persistent selected-document cache, indexed primarily by audio identity.
/// Unknown audio identity falls back to normalized query metadata.
/// Cache files store a schema version and their expected identity key.
/// Invalid or retired-format entries behave as misses, not live documents.
/// Writes use an atomic replacement to preserve the previous good entry.
/// The service separately decides whether a cache hit can be used.
class LyricsCache final {
  public:
    explicit LyricsCache(const QString &directory = {});

    QString directory() const;
    /// Use stable audio identity when available so display edits do not orphan a selection.
    /// Without audio identity, normalized query metadata supplies a fallback cache namespace.
    /// The returned hash is a filename key, not a provider candidate ID.
    QString keyFor(const LyricsQuery &query) const;
    /// Return a validated selected document or no value for absent/unusable cache data.
    /// Invalid schema, mismatched embedded key and retired source formats are cache misses.
    /// No read failure authorizes removal of the existing cache file.
    std::optional<LyricsDocument> read(const LyricsQuery &query) const;
    /// Persist the selected nonempty document, including instrumental-only results.
    /// Failure leaves the caller free to display valid lyrics with a cache warning.
    /// Atomic replacement preserves the previous cache if writing cannot commit.
    bool write(const LyricsQuery &query, const LyricsDocument &document) const;

  private:
    QString m_directory;
};

} // namespace nekotune
