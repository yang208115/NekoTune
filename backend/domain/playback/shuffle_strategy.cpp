#include "domain/playback/shuffle_strategy.h"
#include <algorithm>
#include <QSet>

namespace nekotune {
ShuffleBagStrategy::ShuffleBagStrategy(quint32 seed) : m_random(seed) {}
std::unique_ptr<IShuffleStrategy> ShuffleBagStrategy::clone() const {
    return std::make_unique<ShuffleBagStrategy>(*this);
}
void ShuffleBagStrategy::reset(const QList<int> &queueIds, int currentId) {
    m_ids = queueIds;
    m_pending = queueIds;
    m_pending.removeAll(currentId);
    std::shuffle(m_pending.begin(), m_pending.end(), m_random);
}
void ShuffleBagStrategy::syncQueue(const QList<int> &queueIds, int currentId) {
    if (queueIds == m_ids)
        return;
    const QSet<int> available(queueIds.cbegin(), queueIds.cend());
    const QSet<int> previous(m_ids.cbegin(), m_ids.cend());
    m_pending.removeIf([&](int id) { return !available.contains(id); });
    // Preserve the unplayed bag; rebuilding it on every edit would replay already drawn entries.
    for (int id : queueIds) {
        if (!previous.contains(id) && id != currentId) {
            const auto offset = std::uniform_int_distribution<int>(0, m_pending.size())(m_random);
            m_pending.insert(offset, id);
        }
    }
    m_ids = queueIds;
}
int ShuffleBagStrategy::proposeNext(int currentId) {
    if (m_ids.isEmpty())
        return 0;
    if (m_pending.isEmpty()) {
        m_pending = m_ids;
        std::shuffle(m_pending.begin(), m_pending.end(), m_random);
        // Avoid repeating the last item across bag boundaries, except for a one-item queue.
        if (m_pending.size() > 1 && m_pending.first() == currentId) {
            const auto offset = std::uniform_int_distribution<int>(1, m_pending.size() - 1)(m_random);
            m_pending.swapItemsAt(0, offset);
        }
    }
    return m_pending.first();
}
void ShuffleBagStrategy::confirmSelection(int queueId) { m_pending.removeAll(queueId); }
} // namespace nekotune
