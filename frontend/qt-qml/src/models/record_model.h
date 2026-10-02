#pragma once
#include <QAbstractListModel>
#include <QVariantList>
/// Reconciles ordered snapshots by a unique, stable identity field without resetting QML delegates.
class RecordModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
  public:
    explicit RecordModel(QString identity, QObject *parent = nullptr)
        : QAbstractListModel(parent), m_identity(std::move(identity)) {}
    int count() const { return m_items.size(); }
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : count(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, "modelData"}}; }
    QVariantList items() const { return m_items; }
    /// @param items Ordered record maps carrying the identity field chosen at construction.
    /// Identity values must be stable and unique within the supplied snapshot.
    /// Move existing records, insert new ones and remove the stale suffix without a model reset.
    /// Metadata-only changes emit dataChanged while retaining the same delegate identity.
    /// An identical snapshot produces no change notification.
    void update(const QVariantList &items);
    Q_INVOKABLE QVariantMap get(int row) const {
        return row >= 0 && row < count() ? m_items[row].toMap() : QVariantMap{};
    }
  signals:
    void changed();

  private:
    QString m_identity;
    QVariantList m_items;
};
