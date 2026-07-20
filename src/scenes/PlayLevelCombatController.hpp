#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace playlevel {

struct ActiveEnemy {
    std::string enemyId = "goblin1";
    std::uint64_t runtimeId = 0;
    float distanceAlongPath = 0.0f;
    float health = 1.0f;
    float moveSpeed = 1.0f;
    float rewardMoney = 0.0f;
    float baseDamage = 1.0f;
    float renderScale = 1.0f;
    float facingYawOffsetDegrees = 0.0f;
};

struct PlacedTower {
    std::string towerId;
    glm::vec3 position{0.0f};
    float attackDamage = 1.0f;
    float attackRange = 0.0f;
    float attackIntervalSeconds = 1.0f;
    float attackCooldownRemainingSeconds = 0.0f;
    float projectileSpeed = 16.0f;
    int cost = 0;
};

struct ActiveProjectile {
    std::string towerId;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float damage = 1.0f;
    float remainingLifeSeconds = 0.0f;
    std::uint64_t targetEnemyRuntimeId = 0;
};

} // namespace playlevel

class PlayLevelCombatController {
  public:
    void advanceEnemies(float dt,
                       float routeTotalLength,
                       std::vector<playlevel::ActiveEnemy>& activeEnemies,
                       const std::function<void(float)>& onEnemyReachedBase) const;

    void updateTowerAttacks(float dt,
                            const std::function<glm::vec3(float)>& sampleRoutePosition,
                            std::vector<playlevel::PlacedTower>& placedTowers,
                            const std::vector<playlevel::ActiveEnemy>& activeEnemies,
                            std::vector<playlevel::ActiveProjectile>& activeProjectiles) const;

    void updateProjectiles(float dt,
                           const std::function<glm::vec3(float)>& sampleRoutePosition,
                           std::vector<playlevel::ActiveEnemy>& activeEnemies,
                           std::vector<playlevel::ActiveProjectile>& activeProjectiles) const;

    void collectDefeatedEnemies(std::vector<playlevel::ActiveEnemy>& activeEnemies,
                                const std::function<void(float)>& onRewardGranted,
                                const std::function<void()>& onEnemyDefeated) const;
};
