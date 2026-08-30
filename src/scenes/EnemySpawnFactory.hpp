#pragma once

#include "scenes/EnemyLoadController.hpp"
#include "scenes/PlayLevelCombatController.hpp"

#include <cstdint>

class EnemySpawnFactory {
  public:
    static playlevel::ActiveEnemy create(const std::string& enemyId, const EnemyArchetype* archetype,
                                         std::uint64_t runtimeId, int templatePrototypeIndex);
};
