#pragma once

#include "multiplayer/FixedTickClock.hpp"
#include "multiplayer/PlayerAccounts.hpp"
#include "scenes/PlayLevelCombatController.hpp"
#include "scenes/PlayLevelState.hpp"
#include "scenes/PlayLevelWaveController.hpp"

#include <utility>
#include <vector>

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
    std::vector<PlayerAccounts::Balance> playerBalances() const;
    SimulationTick currentTick() const;
    PlayLevelState& gameplayState();
    const PlayLevelState& gameplayState() const;
    PlayLevelWaveController& waveController();
    PlayLevelCombatController& combatController();
    std::vector<playlevel::PlacedTower>& placedTowers();
    const std::vector<playlevel::PlacedTower>& placedTowers() const;
    std::vector<playlevel::ActiveProjectile>& activeProjectiles();
    const std::vector<playlevel::ActiveProjectile>& activeProjectiles() const;
    std::vector<playlevel::ActiveEnemy>& activeEnemies();
    const std::vector<playlevel::ActiveEnemy>& activeEnemies() const;
    TowerRuntimeId& nextTowerRuntimeId();
    std::uint64_t& nextEnemyRuntimeId();
    std::uint64_t& nextProjectileRuntimeId();

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
    PlayLevelState gameplayState_;
    PlayLevelWaveController waveController_;
    PlayLevelCombatController combatController_;
    std::vector<playlevel::PlacedTower> placedTowers_;
    std::vector<playlevel::ActiveProjectile> activeProjectiles_;
    std::vector<playlevel::ActiveEnemy> activeEnemies_;
    TowerRuntimeId nextTowerRuntimeId_ = 1;
    std::uint64_t nextEnemyRuntimeId_ = 1;
    std::uint64_t nextProjectileRuntimeId_ = 1;
};

} // namespace multiplayer