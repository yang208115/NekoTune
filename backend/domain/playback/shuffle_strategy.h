#pragma once
#include <QList>
#include <QString>
#include <memory>
#include <random>

namespace nekotune {
// Cloning includes random-generator state: an uncommitted proposal consumes nothing.
class IShuffleStrategy {
  public:
    virtual ~IShuffleStrategy() = default;
    virtual QString id() const = 0;
    /// Copy membership, remaining draws and random-generator state together.
    /// Navigation mutates the copy until its queue selection has been saved.
    /// Discarding a proposal must therefore leave the next live draw reproducible.
    virtual std::unique_ptr<IShuffleStrategy> clone() const = 0;
    virtual void reset(const QList<int> &queueIds, int currentId) = 0;
    /// Reconcile occurrence membership while preserving the unplayed portion of this cycle.
    /// New IDs join the remaining choices; removed IDs must not be proposed again.
    /// currentId is excluded from newly admitted draws when alternatives exist.
    virtual void syncQueue(const QList<int> &queueIds, int currentId) = 0;
    /// Return an eligible occurrence ID, or zero when the queue membership is empty.
    /// Drawing may refill/shuffle the bag, so call on a proposal clone before persistence.
    /// confirmSelection consumes the chosen occurrence from that tentative bag.
    virtual int proposeNext(int currentId) = 0;
    virtual void confirmSelection(int queueId) = 0;
};

/// The bag contains queue occurrence IDs not yet chosen this cycle.
/// Duplicate audio entries therefore remain independent choices.
/// The current entry is initially excluded when alternatives exist.
/// New entries join the remaining bag without restarting a cycle.
/// Removed entries disappear from both membership and pending draws.
/// When the bag empties, a new shuffled cycle is generated.
/// The next cycle avoids an immediate repeat when it has alternatives.
/// The seed is injectable for deterministic navigation regression tests.
class ShuffleBagStrategy final : public IShuffleStrategy {
  public:
    explicit ShuffleBagStrategy(quint32 seed = std::random_device{}());
    QString id() const override { return QStringLiteral("shuffle_bag"); }
    std::unique_ptr<IShuffleStrategy> clone() const override;
    void reset(const QList<int> &queueIds, int currentId) override;
    void syncQueue(const QList<int> &queueIds, int currentId) override;
    int proposeNext(int currentId) override;
    void confirmSelection(int queueId) override;

  private:
    QList<int> m_ids;
    QList<int> m_pending;
    std::mt19937 m_random;
};
} // namespace nekotune
