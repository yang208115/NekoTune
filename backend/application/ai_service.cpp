#include "application/ai_service.h"
namespace nekotune {
void AiService::suggest(int songId, const MetadataPatch &draft, IAiBackend::SuggestionCompletion done) {
    const auto song = m_library.metadata(songId);
    if (!song) {
        done(song.error());
        return;
    }
    AiMetadataInput input;
    input.song = song.value();
    input.path = m_library.availablePath(songId);
    input.draft = draft;
    for (const auto &tag : m_library.tagsFor(songId))
        input.currentTags.append(tag.name);
    if (draft.tags)
        input.currentTags = *draft.tags;
    for (const auto &tag : m_tags.tags()) {
        if (input.availableTags.size() == 200)
            break;
        input.availableTags.append(tag.name);
    }
    m_backend.suggest(input, std::move(done));
}
} // namespace nekotune
