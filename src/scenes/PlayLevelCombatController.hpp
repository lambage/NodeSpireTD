#pragma once

#include "scenes/DamageTypes.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

namespace playlevel {

enum class TowerTargetingMode {
    First,
    Last,
    Nearest,
    Random,
    HighestHp,
    LowestHp,
};

const char* towerTargetingModeToString(TowerTargetingMode mode);
bool tryParseTowerTargetingMode(std::string_view rawMode, TowerTargetingMode& outMode);

struct ActiveEnemy {
    std::string enemyId = "goblin1";
    std::uint64_t runtimeId = 0;
    float distanceAlongPath = 0.0f;
    float health = 1.0f;
    float maxHealth = 1.0f;
    float shield = 0.0f;
    float maxShield = 0.0f;
    float armor = 0.0f;
    std::unordered_map<DamageType, float, DamageTypeHash> resistances;
    float moveSpeed = 1.0f;
    float rewardMoney = 0.0f;
    float baseDamage = 1.0f;
    float renderScale = 1.0f;
    float facingYawOffsetDegrees = 0.0f;
};

struct PlacedTower {
    std::string towerId;
    glm::vec3 position{0.0f};
    int towerPrototypeIndex = -1;
    int projectilePrototypeIndex = -1;
    float attackDamage = 1.0f;
    float armorPiercing = 0.0f;
    float attackRange = 0.0f;
    float attackIntervalSeconds = 1.0f;
    float attackCooldownRemainingSeconds = 0.0f;
    float projectileSpeed = 16.0f;
    float splashRadius = 0.0f;
    float chainRange = 3.5f;
    float ricochetRange = 3.5f;
    int projectileCount = 1;
    int chainTargetCount = 1;
    int ricochetCount = 0;
    int cost = 0;
    DamageType damageType = DamageType::Physical;
    TowerTargetingMode targetingMode = TowerTargetingMode::Nearest;
    float totalDamageDealt = 0.0f;
    std::vector<std::string> unlockedUpgradeNodeIds;
};

struct ActiveProjectile {
    std::string towerId;
    int prototypeIndex = -1;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float damage = 1.0f;
    float armorPiercing = 0.0f;
    DamageType damageType = DamageType::Physical;
    float remainingLifeSeconds = 0.0f;
    float splashRadius = 0.0f;
    float chainRange = 3.5f;
    float ricochetRange = 3.5f;
    int chainTargetCount = 1;
    int remainingRicochetCount = 0;
    int sourceTowerPoolIndex = -1;
    std::uint64_t targetEnemyRuntimeId = 0;
    std::uint64_t lastHitEnemyRuntimeId = 0;
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
                           std::vector<playlevel::PlacedTower>& placedTowers,
                           std::vector<playlevel::ActiveEnemy>& activeEnemies,
                           std::vector<playlevel::ActiveProjectile>& activeProjectiles) const;

    void collectDefeatedEnemies(std::vector<playlevel::ActiveEnemy>& activeEnemies,
                                const std::function<void(float)>& onRewardGranted,
                                const std::function<void()>& onEnemyDefeated) const;
};
