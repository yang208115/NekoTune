#pragma once
#include "domain/library/library_types.h"
#include "domain/result.h"
#include <QObject>
#include <functional>

namespace nekotune {
/// This is safe configuration state for the frontend and IPC.
/// It deliberately contains no API key bytes or authorization headers.
/// configured() tests endpoint/model presence, not service reachability.
/// credentialError separately reports keyring availability or missing keys.
/// A configured endpoint may validly operate without authentication.
struct AiConfig {
    QString baseUrl;
    QString model;
    bool keySaved = false;
    QString credentialError;
    bool configured() const { return !baseUrl.isEmpty() && !model.isEmpty(); }
};
/// apiKey absence preserves the selected endpoint's current credential.
/// A present value is validated and written through the secure store.
/// Clearing a credential is a separate operation, not an empty-key save.
/// Endpoint identity prevents a service change from borrowing another key.
struct AiConfigUpdate {
    QString baseUrl;
    QString model;
    std::optional<QString> apiKey;
};
/// Immutable-by-convention value snapshot assembled on the owning backend thread.
/// song supplies persisted metadata; draft contains only the editor's supplied replacements.
/// path permits local lyric lookup on the worker but is excluded from model prompt JSON.
/// currentTags preserve personal classification, while availableTags supply canonical names.
/// This structure transfers values rather than repositories or a live SQL connection.
struct AiMetadataInput {
    SongMetadata song;
    QString path;
    MetadataPatch draft;
    QStringList currentTags;
    QStringList availableTags;
};
/// Advisory draft values only; empty title/artist mean no suggestion, not a request to clear metadata.
struct AiSuggestion {
    QString title;
    QString artist;
    QStringList tags;
    QString warning;
};
/// All operations finish asynchronously through typed completion callbacks.
/// Configuration work may involve a temporarily locked system keyring.
/// Suggestion work receives a value snapshot of the editing context.
/// It must not share the backend's live SQL connection across threads.
/// Suggestion success supplies advisory data rather than saving metadata.
/// test() checks an actual request using a minimal synthetic prompt.
/// Shutdown cancels outstanding callbacks so frontend busy states settle.
class IAiBackend : public QObject {
  public:
    using QObject::QObject;
    using ConfigCompletion = std::function<void(Result<AiConfig>)>;
    using SuggestionCompletion = std::function<void(Result<AiSuggestion>)>;
    virtual void configuration(ConfigCompletion done) = 0;
    virtual void configure(const AiConfigUpdate &update, ConfigCompletion done) = 0;
    virtual void clearKey(ConfigCompletion done) = 0;
    virtual void suggest(const AiMetadataInput &input, SuggestionCompletion done) = 0;
    virtual void test(SuggestionCompletion done) = 0;
    virtual void shutdown() = 0;
};
} // namespace nekotune
