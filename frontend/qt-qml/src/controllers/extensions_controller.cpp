#include "controllers/extensions_controller.h"
#include <QSet>
#include <QTimer>
ExtensionsController::ExtensionsController(IpcClient &client) : FeatureController(client) {
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        const auto name = event.value("event").toString();
        if (name == "server.connected")
            refresh();
        if (name == "extensions.changed")
            apply(event);
        if (name == "extensions.error") {
            m_error = event.value("message").toString();
            emit errorChanged();
        }
        emit eventReceived(event.toVariantMap());
    });
    connect(&client, &IpcClient::connectedChanged, this, [this] {
        if (!m_client.connected()) {
            m_items.clear();
            emit changed();
        }
    });
}
void ExtensionsController::apply(const QJsonObject &data) {
    m_items = data.value("extensions").toVariant().toList();
    m_selections = data.value("selections").toObject().toVariantMap();
    emit changed();
}
void ExtensionsController::refresh() {
    send(
        "extensions.list", {},
        [this](const QJsonObject &data, const QString &error) {
            if (error.isEmpty())
                apply(data);
        },
        false);
}
int ExtensionsController::request(const QString &method, const QVariantMap &params) {
    const int id = m_nextId++;
    // Deliver even disconnected errors after QML has registered its completion listener.
    QTimer::singleShot(0, this, [this, id, method, params] {
        if (qEnvironmentVariable("NEKOTUNE_SAFE_MODE") == "1" &&
            (method == "extensions.enable" || method == "extensions.reload")) {
            m_error = "Restart without --safe-mode to enable extensions";
            emit errorChanged();
            emit completed(id, {}, m_error);
            return;
        }
        auto normalized = params;
        if (method == "extensions.install")
            normalized.insert("path", IpcClient::normalizePath(params.value("path").toString()));
        send(
            method, QJsonObject::fromVariantMap(normalized),
            [this, id](const QJsonObject &data, const QString &error) {
                m_error = error;
                emit errorChanged();
                emit completed(id, data.toVariantMap(), error);
            },
            false);
    });
    return id;
}
QVariantList ExtensionsController::contributions(const QString &kind) const {
    QVariantList result;
    if (qEnvironmentVariable("NEKOTUNE_SAFE_MODE") == "1")
        return result;
    for (const auto &value : m_items) {
        const auto extension = value.toMap();
        if (extension.value("state") != "running")
            continue;
        for (const auto &entry : extension.value("contributes").toMap().value(kind).toList()) {
            auto item = entry.toMap();
            item.insert("generation", extension.value("generation"));
            result.append(item);
        }
    }
    return result;
}
QVariantList ExtensionsController::pages() const {
    auto result = contributions("pages");
    for (auto &value : result) {
        auto item = value.toMap();
        if (!item.contains("group"))
            item.insert("group", "primary");
        value = item;
    }
    return result;
}
QVariantList ExtensionsController::registrations(const QString &kind) const {
    QVariantList result;
    if (qEnvironmentVariable("NEKOTUNE_SAFE_MODE") == "1")
        return result;
    for (const auto &value : m_items)
        result.append(value.toMap().value("registrations").toMap().value(kind).toList());
    return result;
}
QVariantList ExtensionsController::commands() const { return registrations("commands"); }
QVariantList ExtensionsController::sources() const { return registrations("music"); }
QVariantList ExtensionsController::browserSources() const {
    QSet<QString> pageIds;
    for (const auto &value : pages()) {
        const auto page = value.toMap();
        const auto group = page.value("group").toString();
        if (group == "primary" || group == "utility")
            pageIds.insert(page.value("id").toString());
    }
    QVariantList result;
    for (const auto &value : sources()) {
        const auto source = value.toMap();
        // A same-ID page is the default; unrelated pages and settings are not music browsers.
        const auto localPage = source.value("page").toString();
        const auto pageId = localPage.isEmpty()
                                ? source.value("id").toString()
                                : source.value("extensionId").toString() + '/' + localPage;
        if (!pageIds.contains(pageId))
            result.append(value);
    }
    return result;
}
QVariantMap ExtensionsController::selected(const QString &slot) const {
    const auto id = m_selections.value(slot).toString();
    if (id.isEmpty())
        return {};
    for (const auto &value : contributions(slot == "theme" ? "themes" : "slots")) {
        const auto item = value.toMap();
        if (item.value("id") == id && (slot == "theme" || item.value("slot") == slot))
            return item;
    }
    return {};
}
QVariantMap ExtensionsController::themeTokens() const { return selected("theme").value("tokens").toMap(); }
QVariantMap ExtensionsController::activeSlots() const {
    QVariantMap result;
    for (auto it = m_selections.cbegin(); it != m_selections.cend(); ++it) {
        const auto contribution = selected(it.key());
        if (!contribution.isEmpty())
            result.insert(it.key(), contribution);
    }
    return result;
}
QString ExtensionsController::label(const QVariant &value, const QString &language) const {
    const auto map = value.toMap();
    return map.isEmpty() ? value.toString()
                         : map.value(language, map.value(language.left(2), map.value("en", map.value("zh"))))
                               .toString();
}
QVariantList ExtensionsController::menuActions(const QString &context, const QString &language) const {
    QVariantList result;
    for (const auto &value : contributions("menus")) {
        auto item = value.toMap();
        if (item.value("context") != context)
            continue;
        item.insert("key", item.value("id"));
        item.insert("label", label(item.value("title"), language));
        result.append(item);
    }
    return result;
}
void ExtensionsController::execute(const QString &id, const QVariantMap &params) {
    const auto slash = id.indexOf('/');
    if (slash < 0)
        return;
    request("extensions.call", {{"id", id.left(slash)}, {"service", id.mid(slash + 1)}, {"params", params}});
}
void ExtensionsController::resetInterface() {
    for (auto it = m_selections.cbegin(); it != m_selections.cend(); ++it)
        request("extensions.select", {{"slot", it.key()}, {"contribution", ""}});
}
