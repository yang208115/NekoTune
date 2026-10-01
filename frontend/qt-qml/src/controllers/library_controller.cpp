#include "controllers/library_controller.h"
#include <QJsonArray>
#include <QSet>
LibraryController::LibraryController(IpcClient &client) : FeatureController(client) {
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        auto name = event.value("event").toString();
        if (name == "library.changed" || name == "server.connected")
            refreshLibrary();
    });
}
void LibraryController::refreshLibrary() {
    if (m_loading) {
        m_reload = true;
        return;
    }
    m_loading = true;
    send("library.list", {}, [this](const QJsonObject &data, const QString &error) {
        m_loading = false;
        if (error.isEmpty())
            apply(data.value("library").toObject());
        if (m_reload) {
            m_reload = false;
            refreshLibrary();
        }
    });
}
void LibraryController::apply(const QJsonObject &data) {
    m_songs.update(data.value("songs").toArray().toVariantList());
    m_tags.update(data.value("tags").toArray().toVariantList());
    QSet<int> tags, songs;
    for (const auto &item : m_tags.items())
        tags.insert(item.toMap().value("id").toInt());
    for (const auto &item : m_songs.items())
        songs.insert(item.toMap().value("song_id").toInt());
    auto nextTags = m_tagIds;
    nextTags.removeIf([&](const QVariant &id) { return !tags.contains(id.toInt()); });
    if (nextTags != m_tagIds) {
        m_tagIds = nextTags;
        emit filterChanged();
    }
    auto selected = m_songIds;
    selected.removeIf([&](const QVariant &id) { return !songs.contains(id.toInt()); });
    if (selected != m_songIds) {
        m_songIds = selected;
        emit selectionChanged();
    }
    filter();
}
void LibraryController::filter() {
    QVariantList items;
    for (const auto &value : m_songs.items()) {
        auto song = value.toMap();
        QSet<int> assigned;
        for (const auto &tag : song.value("tags").toList())
            assigned.insert(tag.toMap().value("id").toInt());
        bool match = true;
        for (const auto &id : m_tagIds)
            if (!assigned.contains(id.toInt())) {
                match = false;
                break;
            }
        if (match)
            items.append(value);
    }
    m_filtered.update(items);
}
void LibraryController::setSelectedTagIds(const QVariantList &ids) {
    QVariantList normalized;
    for (const auto &id : ids)
        if (!normalized.contains(id.toInt()))
            normalized.append(id.toInt());
    if (normalized == m_tagIds)
        return;
    m_tagIds = normalized;
    clearSelection();
    filter();
    emit filterChanged();
}
void LibraryController::toggleTag(int id) {
    auto next = m_tagIds;
    if (next.contains(id))
        next.removeAll(id);
    else
        next.append(id);
    setSelectedTagIds(next);
}
void LibraryController::toggleSelection(int id) {
    if (m_songIds.contains(id))
        m_songIds.removeAll(id);
    else
        m_songIds.append(id);
    emit selectionChanged();
}
void LibraryController::toggleSelectAll() {
    QVariantList ids;
    bool all = m_filtered.count() > 0;
    for (const auto &item : m_filtered.items()) {
        int id = item.toMap().value("song_id").toInt();
        ids.append(id);
        if (!m_songIds.contains(id))
            all = false;
    }
    m_songIds = all ? QVariantList{} : ids;
    emit selectionChanged();
}
void LibraryController::clearSelection() {
    if (m_songIds.isEmpty())
        return;
    m_songIds.clear();
    emit selectionChanged();
}
void LibraryController::playLibrary(const QVariantList &tagIds, int songId) {
    QJsonObject params{{"tag_ids", QJsonArray::fromVariantList(tagIds)}};
    if (songId > 0)
        params.insert("song_id", songId);
    send("library.play", params, [this](const QJsonObject &data, const QString &error) {
        if (error.isEmpty())
            emit libraryPlaybackSkipped(data.value("skipped_song_ids").toArray().size());
    });
}
void LibraryController::deleteLibrarySongs(const QVariantList &ids) {
    send("library.delete", {{"song_ids", QJsonArray::fromVariantList(ids)}},
         [this](const auto &, const QString &error) {
             if (error.isEmpty())
                 clearSelection();
         });
}
void LibraryController::loadMetadata(int id) {
    m_editingId = id;
    m_metadataReady = false;
    m_editing.clear();
    emit editingChanged();
    auto generation = ++m_generation;
    send("song.metadata", {{"song_id", id}},
         [this, id, generation](const QJsonObject &data, const QString &error) {
             if (generation != m_generation || id != m_editingId)
                 return;
             if (error.isEmpty()) {
                 m_editing = data.toVariantMap();
                 m_metadataReady = true;
             }
             emit editingChanged();
         });
}
void LibraryController::saveMetadata(const QVariantMap &patch) {
    if (!m_metadataReady)
        return;
    auto data = QJsonObject::fromVariantMap(patch);
    data.insert("song_id", m_editingId);
    int id = m_editingId;
    send("song.update_metadata", data, [this, id](const auto &, const QString &error) {
        if (error.isEmpty())
            emit songMetadataSaved(id);
    });
}
