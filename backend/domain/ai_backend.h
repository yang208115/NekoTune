#pragma once
#include "domain/library/library_types.h"
#include "domain/result.h"
#include <QObject>
#include <functional>

namespace nekotune {
struct AiConfig {
    QString baseUrl;
    QString model;
    bool keySaved = false;
    QString credentialError;
    bool configured() const { return !baseUrl.isEmpty() && !model.isEmpty(); }
};
struct AiConfigUpdate {
    QString baseUrl;
    QString model;
    std::optional<QString> apiKey;
};
struct AiMetadataInput {
    SongMetadata song;
    QString path;
    MetadataPatch draft;
    QStringList currentTags;
    QStringList availableTags;
};
struct AiSuggestion {
    QString title;
    QString artist;
    QStringList tags;
    QString warning;
};
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
