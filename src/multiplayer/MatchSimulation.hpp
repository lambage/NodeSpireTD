#pragma once

#include "multiplayer/FixedTickClock.hpp"
#include "multiplayer/PlayerAccounts.hpp"

#include <utility>

namespace multiplayer {

class MatchSimulation {
  public:
    void reset();

    bool registerPlayer(PlayerId playerId, float initialBalance);
    bool unregisterPlayer(PlayerId playerId);
    bool hasPlayer(PlayerId playerId) const;
    float playerBalance(PlayerId playerId) const;
    bool debitPlayer(PlayerId playerId, float amount);
    bool creditPlayer(PlayerId playerId, float amount);
    SimulationTick currentTick() const;

    template <typename AdvanceTickFn>
    void advance(float elapsedSeconds, AdvanceTickFn&& advanceTick) {
        const std::uint32_t tickCount = clock_.consumeTicks(elapsedSeconds);
        for (std::uint32_t tick = 0; tick < tickCount; ++tick) {
            ++currentTick_;
            std::forward<AdvanceTickFn>(advanceTick)(currentTick_, FixedTickClock::kTickSeconds);
        }
    }

  private:
    PlayerAccounts playerAccounts_;
    FixedTickClock clock_;
    SimulationTick currentTick_ = 0;
};

} // namespace multiplayer