#include "models/record_model.h"
QVariant RecordModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count() || role != Qt::UserRole + 1)
        return {};
    return m_items[index.row()];
}
void RecordModel::update(const QVariantList &items) {
    if (items == m_items)
        return;
    for (int row = 0; row < items.size(); ++row) {
        auto key = items[row].toMap().value(m_identity);
        int found = -1;
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
            beginMoveRows({}, found, found, {}, row);
            m_items.move(found, row);
            endMoveRows();
        }
        if (m_items[row] != items[row]) {
            m_items[row] = items[row];
            emit dataChanged(index(row), index(row), {Qt::UserRole + 1});
        }
    }
    if (m_items.size() > items.size()) {
        beginRemoveRows({}, items.size(), m_items.size() - 1);
        while (m_items.size() > items.size())
            m_items.removeLast();
        endRemoveRows();
    }
    emit changed();
}
