#pragma once

#include "core/player_engine.h"

#include <QJsonObject>

namespace nekotune {

class IpcRouter final {
public:
    explicit IpcRouter(PlayerEngine &player);

    QJsonObject dispatch(const QJsonObject &request) const;

private:
    PlayerEngine &m_player;
};

} // namespace nekotune
