#include "application/playback/playback_order_service.h"
#include <QSet>

namespace nekotune {
namespace {
QList<int> ids(const PlayerQueue &queue) {
    QList<int> result;
    for (int i = 0; i < queue.size(); ++i)
        result.append(queue.at(i).id);
    return result;
}
int currentId(const PlayerQueue &queue) {
    return queue.currentIndex() < 0 ? 0 : queue.at(queue.currentIndex()).id;
}
} // namespace
PlaybackOrderService::PlaybackOrderService(std::unique_ptr<IShuffleStrategy> strategy,
                                         PlaybackMode mode, ModeSaver save)
    : m_mode(mode), m_save(std::move(save)), m_shuffle(std::move(strategy)) {}
Result<void> PlaybackOrderService::setMode(PlaybackMode mode, const PlayerQueue &queue) {
    if (m_mode == mode)
        return {};
    // A failed settings write must not leave the UI and the next startup in different modes.
    if (m_save && !m_save(mode))
        return failure(QStringLiteral("Unable to save playback mode"), ErrorCode::Storage);
    m_mode = mode;
    reset(queue);
    return {};
}
void PlaybackOrderService::reset(const PlayerQueue &queue) {
    m_currentId = currentId(queue);
    if (m_mode == PlaybackMode::Shuffle)
        m_shuffle->reset(ids(queue), m_currentId);
    m_history.clear();
    if (m_currentId)
        m_history.append(m_currentId);
    m_cursor = m_history.size() - 1;
}
// Queue edits can remove historical occurrences or append new ones.
// Filter history by occurrence identity while preserving its order.
// Remap the cursor to the last surviving entry at/before its position.
// A changed selection becomes a new history branch after that cursor.
// This distinguishes external selection from confirmed navigation.
// PlayerEngine suppresses this path while committing a proposal.
void PlaybackOrderService::syncQueue(const PlayerQueue &queue) {
    const auto queueIds = ids(queue);
    const int selected = currentId(queue);
    const QSet<int> available(queueIds.cbegin(), queueIds.cend());
    if (m_mode == PlaybackMode::Shuffle)
        m_shuffle->syncQueue(queueIds, selected);
    QList<int> history;
    int cursor = -1;
    for (int i = 0; i < m_history.size(); ++i) {
        if (available.contains(m_history.at(i))) {
            history.append(m_history.at(i));
            if (i <= m_cursor)
                cursor = history.size() - 1;
        }
    }
    m_history = std::move(history);
    m_cursor = cursor;
    if (selected != m_currentId && selected) {
        if (m_mode == PlaybackMode::Shuffle)
            m_shuffle->confirmSelection(selected);
        // Removal can select a surviving item; discard the obsolete forward branch.
        m_history = m_history.mid(0, m_cursor + 1);
        m_history.append(selected);
        m_cursor = m_history.size() - 1;
    }
    m_currentId = selected;
}
PlaybackOrderService::Selection PlaybackOrderService::propose(const PlayerQueue &queue,
                                                             PlaybackAdvance advance) const {
    Selection result;
    if (queue.isEmpty())
        return result;
    const int current = currentId(queue);
    if (m_mode == PlaybackMode::Shuffle) {
        result.shuffle = m_shuffle->clone();
        result.history = m_history;
        result.cursor = m_cursor;
        if (advance == PlaybackAdvance::Previous) {
            if (result.cursor > 0)
                --result.cursor;
            result.queueId = result.cursor >= 0 ? result.history.at(result.cursor)
                                                : (current ? current : queue.at(0).id);
            if (result.cursor < 0) {
                result.history.append(result.queueId);
                result.cursor = 0;
                result.shuffle->confirmSelection(result.queueId);
            }
        } else if (result.cursor + 1 < result.history.size()) {
            // After Previous, Next retraces actual history rather than drawing another random item.
            result.queueId = result.history.at(++result.cursor);
        } else {
            result.queueId = result.shuffle->proposeNext(current);
            result.shuffle->confirmSelection(result.queueId);
            result.history.append(result.queueId);
            result.cursor = result.history.size() - 1;
        }
        return result;
    }
    int index = queue.currentIndex();
    if (advance == PlaybackAdvance::Ended && m_mode == PlaybackMode::RepeatOne && index >= 0) {
        result.queueId = current;
        return result;
    }
    if (advance == PlaybackAdvance::Previous)
        index = index <= 0 ? (m_mode == PlaybackMode::RepeatAll ? queue.size() - 1 : 0) : index - 1;
    else {
        ++index;
        if (index >= queue.size()) {
            if (m_mode != PlaybackMode::RepeatAll)
                return result;
            index = 0;
        }
    }
    result.queueId = queue.at(index).id;
    return result;
}
// Selection carries a clone, so proposal never consumed live randomness.
// Replace the clone and cursor only after the queue commit succeeds.
// Previous/Next navigation can now replay the same recorded history.
// Nonshuffle selections only need to update the current identity.
void PlaybackOrderService::confirm(Selection selection) {
    m_currentId = selection.queueId;
    if (selection.shuffle) {
        m_shuffle = std::move(selection.shuffle);
        m_history = std::move(selection.history);
        m_cursor = selection.cursor;
    }
}
} // namespace nekotune
