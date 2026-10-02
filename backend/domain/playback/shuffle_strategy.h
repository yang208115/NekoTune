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
    virtual std::unique_ptr<IShuffleStrategy> clone() const = 0;
    virtual void reset(const QList<int> &queueIds, int currentId) = 0;
    virtual void syncQueue(const QList<int> &queueIds, int currentId) = 0;
    virtual int proposeNext(int currentId) = 0;
    virtual void confirmSelection(int queueId) = 0;
};

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
