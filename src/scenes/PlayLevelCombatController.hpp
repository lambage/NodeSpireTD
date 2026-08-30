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

// Coarse per-enemy lifecycle used to drive death animation/removal timing:
//  Alive  - normal simulation: moves along the path, can be targeted/damaged.
//  Dying  - health has reached zero; movement/targeting/collision stop immediately, but the
//           enemy stays in activeEnemies_ so its Death clip can keep playing to completion.
//  Dead   - the Death clip has finished playing; the enemy is removed on the same pass.
enum class EnemyLifecycleState {
    Alive,
    Dying,
    Dead,
};

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
    // Copied from this enemy's EnemyArchetype at spawn time (see EnemyLoadController.hpp) so
    // clip selection stays data-driven per archetype rather than hardcoded in C++.
    std::string idleClipName = "Idle";
    std::string walkingClipName = "Walking";
    std::string deathClipName = "Death";
    // Which animated enemy template (WorldAssetSpec::animatedTemplateModelPaths index / the
    // templatePrototypeIndex carried on WorldMesh) this enemy's own model/skeleton/animator maps
    // to. Resolved once at spawn time (see findEnemyPrototypeIndex in PlayLevelScene.cpp) so every
    // per-frame animation lookup routes to THIS enemy's own TemplateAnimator rather than
    // whichever template happened to load first.
    int templatePrototypeIndex = 0;
    EnemyLifecycleState lifecycleState = EnemyLifecycleState::Alive;
    float deathElapsedSeconds = 0.0f;
    // Elapsed simulation time (seconds) this enemy has spent Alive/walking, used to drive its own
    // independent Walking clip playback time (see PlayLevelScene::syncTowerInstanceTransforms) --
    // TemplateAnimator::setPlaybackTimeSeconds() wraps this modulo the clip's duration, so a
    // continuously-growing value here loops correctly without any extra bookkeeping.
    float walkAnimElapsedSeconds = 0.0f;
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

    // Advances enemies already in the Dying state and removes ones whose Death clip has finished
    // playing (deathElapsedSeconds >= that enemy's own Death clip duration). lookupDeathClipDurationSeconds
    // is called with each Dying enemy (so the caller can resolve both its own deathClipName AND its
    // own templatePrototypeIndex -- archetypes can name their Death clip differently AND use
    // different models/animators) and should return 0 when the active model has no clip by that
    // name -- such enemies are then removed immediately, same as the old instant-removal behavior.
    void advanceDyingEnemies(float dt,
                             const std::function<float(const playlevel::ActiveEnemy&)>& lookupDeathClipDurationSeconds,
                             std::vector<playlevel::ActiveEnemy>& activeEnemies) const;
};
