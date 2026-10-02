#include "models/record_model.h"
QVariant RecordModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count() || role != Qt::UserRole + 1)
        return {};
    return m_items[index.row()];
}
void RecordModel::update(const QVariantList &items) {
    if (items == m_items)
        return;
    // Fix each prefix in place with insert/move/change signals; resetting the model would discard
    // delegate state and disrupt selection or scrolling when only metadata changed.
    for (int row = 0; row < items.size(); ++row) {
        auto key = items[row].toMap().value(m_identity);
        int found = -1;
        // Rows before this one already match the desired prefix and must not be searched again.
        // The matching record can only come from the unreconciled suffix.
        // Unique identity values are therefore a caller constraint rather than an inferred index key.
        for (int i = row; i < m_items.size(); ++i)
            if (m_items[i].toMap().value(m_identity) == key) {
                found = i;
                break;
            }
        if (found < 0) {
            beginInsertRows({}, row, row);
            m_items.insert(row, items[row]);
            endInsertRows();
        } else if (found != row) {
            // The suffix search guarantees found > row, so this is always an upward move.
            // Qt's destination row is interpreted before removal; no downward-offset adjustment is needed.
            // Retaining this invariant avoids an off-by-one when reconciling reordered snapshots.
            beginMoveRows({}, found, found, {}, row);
            m_items.move(found, row);
            endMoveRows();
        }
        if (m_items[row] != items[row]) {
            m_items[row] = items[row];
            emit dataChanged(index(row), index(row), {Qt::UserRole + 1});
        }
    }
    // After prefix reconciliation, all remaining old records form one removable suffix.
    // Notify one contiguous removal rather than resetting the entire model.
    // The surviving delegates keep selection and scroll-related state across that cleanup.
    if (m_items.size() > items.size()) {
        beginRemoveRows({}, items.size(), m_items.size() - 1);
        while (m_items.size() > items.size())
            m_items.removeLast();
        endRemoveRows();
    }
    emit changed();
}
