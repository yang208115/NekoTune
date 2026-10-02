#include "infrastructure/ai/ai_settings.h"
#include "app_paths.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QUrl>
#include <algorithm>

namespace nekotune {
Result<QString> AiSettings::normalizeBaseUrl(const QString &value) {
    QUrl url(value.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() || value.size() > 2048)
        return failure("ai_error_url");
    url = url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash);
    return url.toString(QUrl::FullyEncoded);
}
QString AiSettings::endpointId(const QString &baseUrl) {
    return QString::fromLatin1(
        QCryptographicHash::hash(baseUrl.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString AiSettings::credentialId(const QString &baseUrl) {
    // Namespace keys by profile and endpoint so changing services cannot reuse another service's secret.
    return AppPaths::configFile("ai-key-" + endpointId(baseUrl));
}
Result<QByteArray> AiSettings::key(const AiConfig &config) const {
    // Keyless endpoints work even on desktops without an unlocked keyring.
    if (!config.keySaved)
        return QByteArray{};
    const auto stored = m_store->read(credentialId(config.baseUrl));
    if (!stored)
        return failure("ai_error_keyring", ErrorCode::Storage);
    if (!stored.value() || stored.value()->isEmpty())
        return failure("ai_error_key_missing", ErrorCode::Storage);
    return *stored.value();
}
AiConfig AiSettings::configuration() const {
    const auto object = AppPaths::setting("ai").toObject();
    AiConfig config{object.value("base_url").toString(), object.value("model").toString()};
    config.keySaved = object.value("key_endpoints").toArray().contains(endpointId(config.baseUrl));
    const auto secret = key(config);
    if (!secret)
        config.credentialError = secret.error().message;
    return config;
}
Result<AiConfig> AiSettings::configure(const AiConfigUpdate &update) {
    const auto url = normalizeBaseUrl(update.baseUrl);
    if (!url)
        return url.error();
    const auto model = update.model.trimmed();
    if (model.isEmpty() || model.size() > 256 || model.contains(QChar::Null))
        return failure("ai_error_model");
    if (update.apiKey && (update.apiKey->trimmed().isEmpty() || update.apiKey->size() > 4096 ||
                          std::any_of(update.apiKey->cbegin(), update.apiKey->cend(),
                                      [](QChar c) { return c.unicode() < 32 || c.unicode() > 126; })))
        return failure("ai_error_key");
    auto object = AppPaths::setting("ai").toObject();
    auto endpoints = object.value("key_endpoints").toArray();
    const auto id = credentialId(url.value());
    std::optional<QByteArray> previous;
    if (update.apiKey) {
        const auto old = m_store->read(id);
        if (!old)
            return failure("ai_error_keyring", ErrorCode::Storage);
        previous = old.value();
        if (!m_store->write(id, update.apiKey->trimmed().toUtf8()))
            return failure("ai_error_keyring", ErrorCode::Storage);
        const auto verified = m_store->read(id);
        if (!verified || !verified.value() || *verified.value() != update.apiKey->trimmed().toUtf8()) {
            if (previous)
                m_store->write(id, *previous);
            else
                m_store->remove(id);
            return failure("ai_error_keyring", ErrorCode::Storage);
        }
        if (!endpoints.contains(endpointId(url.value())))
            endpoints.append(endpointId(url.value()));
    }
    object.insert("base_url", url.value());
    object.insert("model", model);
    object.insert("key_endpoints", endpoints);
    if (!AppPaths::saveSetting("ai", object)) {
        // Settings and the native keyring cannot share a transaction; restore the previous secret on failure.
        if (update.apiKey) {
            const auto restored = previous ? m_store->write(id, *previous) : m_store->remove(id);
            if (!restored)
                return failure("ai_error_keyring", ErrorCode::Storage);
        }
        return failure("ai_error_settings", ErrorCode::Storage);
    }
    return configuration();
}
Result<AiConfig> AiSettings::clearKey() {
    auto object = AppPaths::setting("ai").toObject();
    const auto url = object.value("base_url").toString();
    auto endpoints = object.value("key_endpoints").toArray();
    if (!endpoints.contains(endpointId(url)))
        return configuration();
    const auto id = credentialId(url);
    const auto old = m_store->read(id);
    if (!old || !m_store->remove(id))
        return failure("ai_error_keyring", ErrorCode::Storage);
    QJsonArray remaining;
    for (const auto &endpoint : endpoints)
        if (endpoint != endpointId(url))
            remaining.append(endpoint);
    object.insert("key_endpoints", remaining);
    if (!AppPaths::saveSetting("ai", object)) {
        if (old.value() && !m_store->write(id, *old.value()))
            return failure("ai_error_keyring", ErrorCode::Storage);
        return failure("ai_error_settings", ErrorCode::Storage);
    }
    return configuration();
}
} // namespace nekotune
