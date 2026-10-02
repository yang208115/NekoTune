#pragma once
#include "domain/playback/playback_mode.h"
#include "domain/playback/player_queue.h"
#include "domain/result.h"
#include "domain/playback/shuffle_strategy.h"
#include <functional>

namespace nekotune {
enum class PlaybackAdvance { Ended, Next, Previous };
class PlaybackOrderService final {
  public:
    using ModeSaver = std::function<bool(PlaybackMode)>;
    struct Selection {
        int queueId = 0; // Zero means stop; queue IDs are positive.
        std::unique_ptr<IShuffleStrategy> shuffle;
        QList<int> history;
        int cursor = -1;
    };
    explicit PlaybackOrderService(
        std::unique_ptr<IShuffleStrategy> strategy = std::make_unique<ShuffleBagStrategy>(),
        PlaybackMode mode = PlaybackMode::Sequential, ModeSaver save = {});
    PlaybackMode mode() const { return m_mode; }
    Result<void> setMode(PlaybackMode mode, const PlayerQueue &queue);
    void reset(const PlayerQueue &queue);
    void syncQueue(const PlayerQueue &queue);
    Selection propose(const PlayerQueue &queue, PlaybackAdvance advance) const;
    void confirm(Selection selection);

  private:
    PlaybackMode m_mode;
    ModeSaver m_save;
    std::unique_ptr<IShuffleStrategy> m_shuffle;
    QList<int> m_history;
    int m_cursor = -1;
    int m_currentId = 0;
};
} // namespace nekotune
