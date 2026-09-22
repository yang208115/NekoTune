#include "core/player_queue.h"

#include <QFileInfo>

namespace nekotune {

namespace {

QString displayTitle(const QueueItem &item)
{
    const QString customTitle = item.metadata.customTitle.trimmed();
    if (!customTitle.isEmpty()) {
        return customTitle;
    }

    return QFileInfo(item.path).completeBaseName();
}

QJsonObject itemToObject(const QueueItem &item, int position, bool includeLyrics)
{
    QJsonObject object {
        {QStringLiteral("folder_id"), item.folderId},
        {QStringLiteral("id"), item.id},
        {QStringLiteral("queue_id"), item.id},
        {QStringLiteral("song_id"), item.metadata.id},
        {QStringLiteral("song_hash"), item.metadata.hash},
        {QStringLiteral("path"), item.path},
        {QStringLiteral("first_path"), item.metadata.firstPath},
        {QStringLiteral("title"), displayTitle(item)},
        {QStringLiteral("custom_title"), item.metadata.customTitle},
        {QStringLiteral("artist"), item.metadata.artist},
    };

    if (position >= 0) {
        object.insert(QStringLiteral("position"), position);
        object.insert(QStringLiteral("state"), item.state);
    }

    if (includeLyrics) {
        object.insert(QStringLiteral("lyrics"), item.metadata.lyrics);
    }

    return object;
}

} // namespace

QString PlayerQueue::organize(const QString &action, const QJsonObject &params)
{
    auto validId = [&](const QString &key) {
        const auto value = params.value(key);
        return value.isDouble() && value.toDouble() >= 0
            && value.toDouble() == value.toInt(-1);
    };
    auto folderIndex = [&](int id) {
        for (int i = 0; i < folders.size(); ++i)
            if (folders[i].toObject().value("id").toInt() == id) return i;
        return -1;
    };
    const int id = params.value("id").toInt();
    const int parent = params.value("parent_id").toInt();
    if (action != "create" && (!validId("id") || id == 0))
        return QStringLiteral("Invalid item id");
    if (action == "create" || action == "move" || action == "move_item") {
        if (!validId("parent_id") || (parent != 0 && folderIndex(parent) < 0))
            return QStringLiteral("Folder does not exist");
    }
    const int index = folderIndex(id);
    if (action != "create" && action != "move_item" && index < 0)
        return QStringLiteral("Folder does not exist");
    if (action == "create" || action == "rename") {
        const auto name = params.value("name").toString().trimmed();
        if (name.isEmpty() || name.size() > 128)
            return QStringLiteral("Folder name must contain 1 to 128 characters");
        int nextId = 1;
        for (const auto &value : folders)
            nextId = qMax(nextId, value.toObject().value("id").toInt() + 1);
        auto folder = action == "create" ? QJsonObject{{"id", nextId}, {"parent_id", parent}}
                                         : folders[index].toObject();
        folder.insert("name", name);
        if (action == "create") folders.append(folder);
        else folders[index] = folder;
    } else if (action == "move_item") {
        const int itemIndex = indexById(id);
        if (itemIndex < 0) return QStringLiteral("Queue item does not exist");
        m_items[itemIndex].folderId = parent;
    } else if (action == "move") {
        int ancestor = parent;
        while (ancestor != 0) {
            if (ancestor == id) return QStringLiteral("Cannot move a folder into itself or its descendants");
            ancestor = folders[folderIndex(ancestor)].toObject().value("parent_id").toInt();
        }
        auto folder = folders[index].toObject();
        folder.insert("parent_id", parent);
        folders[index] = folder;
    } else if (action == "delete") {
        const int destination = folders[index].toObject().value("parent_id").toInt();
        for (auto &item : m_items)
            if (item.folderId == id) item.folderId = destination;
        for (int i = 0; i < folders.size(); ++i) {
            auto folder = folders[i].toObject();
            if (folder.value("parent_id").toInt() == id) {
                folder.insert("parent_id", destination);
                folders[i] = folder;
            }
        }
        folders.removeAt(index);
    } else {
        return QStringLiteral("Unknown folder action");
    }
    return {};
}

bool PlayerQueue::isEmpty() const
{
    return m_items.isEmpty();
}

int PlayerQueue::size() const
{
    return m_items.size();
}

int PlayerQueue::currentIndex() const
{
    return m_currentIndex;
}

bool PlayerQueue::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_items.size()) {
        return false;
    }

    m_currentIndex = index;
    return true;
}

int PlayerQueue::add(const QString &path, const SongMetadata &metadata)
{
    const int id = m_nextQueueId++;
    m_items.append({
        id,
        metadata,
        path,
        QStringLiteral("queued"),
    });
    return id;
}

bool PlayerQueue::removeAt(int index)
{
    if (index < 0 || index >= m_items.size()) {
        return false;
    }

    const int previousCurrentIndex = m_currentIndex;
    m_items.removeAt(index);

    if (index == previousCurrentIndex) {
        m_currentIndex = -1;
    } else if (index < previousCurrentIndex) {
        --m_currentIndex;
    } else if (m_currentIndex >= m_items.size()) {
        m_currentIndex = -1;
    }

    return true;
}

void PlayerQueue::clear()
{
    m_items.clear();
    m_currentIndex = -1;
}

void PlayerQueue::markCurrent()
{
    for (auto &item : m_items) {
        item.state = QStringLiteral("queued");
    }

    if (m_currentIndex >= 0 && m_currentIndex < m_items.size()) {
        m_items[m_currentIndex].state = QStringLiteral("current");
    }
}

void PlayerQueue::updateSongMetadata(const SongMetadata &metadata)
{
    for (auto &item : m_items) {
        if (item.metadata.id == metadata.id) {
            item.metadata = metadata;
        }
    }
}

QueueItem &PlayerQueue::operator[](int index)
{
    return m_items[index];
}

const QueueItem &PlayerQueue::at(int index) const
{
    return m_items.at(index);
}

int PlayerQueue::indexById(int queueId) const
{
    for (int index = 0; index < m_items.size(); ++index) {
        if (m_items.at(index).id == queueId) {
            return index;
        }
    }

    return -1;
}

QJsonObject PlayerQueue::currentSongObject() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return {};
    }

    return itemToObject(m_items.at(m_currentIndex), -1, true);
}

QJsonArray PlayerQueue::toArray() const
{
    QJsonArray items;
    for (int index = 0; index < m_items.size(); ++index) {
        items.append(itemToObject(m_items.at(index), index, false));
    }
    return items;
}

QVector<QueueRecord> PlayerQueue::records() const
{
    QVector<QueueRecord> records;
    records.reserve(m_items.size());
    for (const auto &item : m_items) {
        records.append({item.path, item.metadata.id, item.folderId});
    }
    return records;
}

void PlayerQueue::restore(const QVector<QueueItem> &items, int currentIndex)
{
    m_items = items;
    m_currentIndex = -1;
    m_nextQueueId = 1;
    for (const auto &item : m_items) {
        m_nextQueueId = qMax(m_nextQueueId, item.id + 1);
    }
    setCurrentIndex(currentIndex);
    markCurrent();
}

} // namespace nekotune
