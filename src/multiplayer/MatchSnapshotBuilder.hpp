#pragma once

#include "multiplayer/MatchSimulation.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace multiplayer {

// Plain, wire-shaped mirror of a MatchSnapshot -- deliberately not the same types as
// MatchSimulation's own containers (e.g. PlacedTower), since a client only needs the fields
// required to render/display remote state, not host-only combat-stat bookkeeping.
struct DecodedMatchSnapshot {
    struct PlayerBalance {
        PlayerId playerId = 0;
        float money = 0.0f;
    };
    struct Tower {
        TowerRuntimeId runtimeId = 0;
        PlayerId ownerPlayerId = 0;
        std::string towerArchetypeId;
        float positionX = 0.0f;
        float positionY = 0.0f;
        float positionZ = 0.0f;
        TowerTargetingMode targetingMode = TowerTargetingMode::First;
        std::vector<std::string> unlockedUpgradeNodeIds;
    };
    struct Enemy {
        std::uint64_t runtimeId = 0;
        std::string enemyArchetypeId;
        float distanceAlongPath = 0.0f;
        float health = 0.0f;
        float shield = 0.0f;
        std::uint32_t lifecycleState = 0;
    };
    struct Projectile {
        std::uint64_t runtimeId = 0;
        std::string towerArchetypeId;
        float positionX = 0.0f;
        float positionY = 0.0f;
        float positionZ = 0.0f;
        std::uint64_t targetEnemyRuntimeId = 0;
    };

    SimulationTick simulationTick = 0;
    MatchStatus matchStatus = MatchStatus::WaitingToStart;
    float baseHealth = 0.0f;
    int currentWave = 0;
    bool waveInProgress = false;
    std::vector<PlayerBalance> players;
    std::vector<Tower> towers;
    std::vector<Enemy> enemies;
    std::vector<Projectile> projectiles;
};

class MatchSnapshotBuilder {
  public:
    static std::optional<std::string> serialize(const MatchSimulation& simulation);
    static std::optional<DecodedMatchSnapshot> deserialize(std::string_view payload);
};

} // namespace multiplayer
