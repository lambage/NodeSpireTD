#include "scenes/PlayLevelCombatController.hpp"

#include <algorithm>
#include <cctype>
#include <glm/geometric.hpp>
#include <limits>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>

namespace {

constexpr float kProjectileHitRadius = 0.45f;
constexpr float kProjectileLifetimeSeconds = 2.0f;

float computeMitigatedDamage(float incomingDamage, float armor, float armorPiercing) {
    const float effectiveArmor = std::max(0.0f, armor - std::max(0.0f, armorPiercing));
    return std::max(0.0f, incomingDamage - effectiveArmor);
}

float computeDamageDelta(float incomingDamage, float armor, float armorPiercing, float resistancePercent) {
    const float multiplier = 1.0f - (resistancePercent * 0.01f);
    float delta = incomingDamage * multiplier;
    if (delta > 0.0f) {
        delta = computeMitigatedDamage(delta, armor, armorPiercing);
    }
    return delta;
}

} // namespace

const char* playlevel::towerTargetingModeToString(TowerTargetingMode mode) {
    switch (mode) {
    case TowerTargetingMode::First:
        return "first";
    case TowerTargetingMode::Last:
        return "last";
    case TowerTargetingMode::Nearest:
        return "nearest";
    case TowerTargetingMode::Random:
        return "random";
    case TowerTargetingMode::HighestHp:
        return "highest_hp";
    case TowerTargetingMode::LowestHp:
        return "lowest_hp";
    }
    return "nearest";
}

bool playlevel::tryParseTowerTargetingMode(std::string_view rawMode, TowerTargetingMode& outMode) {
    std::string normalized;
    normalized.reserve(rawMode.size());
    for (char c : rawMode) {
        if (c == ' ' || c == '-' || c == '_') {
            normalized.push_back('_');
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (normalized == "first") {
        outMode = TowerTargetingMode::First;
        return true;
    }
    if (normalized == "last") {
        outMode = TowerTargetingMode::Last;
        return true;
    }
    if (normalized == "nearest") {
        outMode = TowerTargetingMode::Nearest;
        return true;
    }
    if (normalized == "random") {
        outMode = TowerTargetingMode::Random;
        return true;
    }
    if (normalized == "highest_hp" || normalized == "highesthp") {
        outMode = TowerTargetingMode::HighestHp;
        return true;
    }
    if (normalized == "lowest_hp" || normalized == "lowesthp") {
        outMode = TowerTargetingMode::LowestHp;
        return true;
    }
    return false;
}

void PlayLevelCombatController::advanceEnemies(float dt,
                                               float routeTotalLength,
                                               std::vector<playlevel::ActiveEnemy>& activeEnemies,
                                               const std::function<void(float)>& onEnemyReachedBase) const {
    std::size_t writeIndex = 0;
    for (std::size_t i = 0; i < activeEnemies.size(); ++i) {
        playlevel::ActiveEnemy enemy = activeEnemies[i];

        if (enemy.lifecycleState != playlevel::EnemyLifecycleState::Alive) {
            // Already dying/dead: no path movement, no base damage -- it's just playing out its
            // death animation in place. advanceDyingEnemies() (not this function) decides when it
            // is actually removed.
            activeEnemies[writeIndex++] = std::move(enemy);
            continue;
        }

        enemy.distanceAlongPath += std::max(0.05f, enemy.moveSpeed) * dt;
        // Drives this enemy's own per-instance Walking clip playback time (see
        // PlayLevelScene::syncTowerInstanceTransforms) -- only accumulated while actually Alive,
        // matching this branch's "still walking" scope.
        enemy.walkAnimElapsedSeconds += dt;

        if (enemy.distanceAlongPath >= routeTotalLength) {
            onEnemyReachedBase(std::max(1.0f, enemy.baseDamage));
            continue;
        }

        activeEnemies[writeIndex++] = std::move(enemy);
    }
    activeEnemies.resize(writeIndex);
}

void PlayLevelCombatController::updateTowerAttacks(
    float dt,
    const std::function<glm::vec3(float)>& sampleRoutePosition,
    std::vector<playlevel::PlacedTower>& placedTowers,
    const std::vector<playlevel::ActiveEnemy>& activeEnemies,
    std::vector<playlevel::ActiveProjectile>& activeProjectiles) const {
    for (int towerIndex = 0; towerIndex < static_cast<int>(placedTowers.size()); ++towerIndex) {
        playlevel::PlacedTower& tower = placedTowers[static_cast<std::size_t>(towerIndex)];
        tower.attackCooldownRemainingSeconds = std::max(0.0f, tower.attackCooldownRemainingSeconds - dt);
        if (tower.attackCooldownRemainingSeconds > 0.0f) {
            continue;
        }

        const float attackRangeSq = tower.attackRange * tower.attackRange;
        std::vector<std::pair<float, int>> candidateTargets;
        candidateTargets.reserve(activeEnemies.size());

        for (int i = 0; i < static_cast<int>(activeEnemies.size()); ++i) {
            const playlevel::ActiveEnemy& enemy = activeEnemies[static_cast<std::size_t>(i)];
            if (enemy.lifecycleState != playlevel::EnemyLifecycleState::Alive) {
                continue;
            }
            const glm::vec3 enemyPos = sampleRoutePosition(enemy.distanceAlongPath);
            const glm::vec3 delta = enemyPos - tower.position;
            const float distSq = glm::dot(delta, delta);
            if (distSq <= attackRangeSq) {
                candidateTargets.emplace_back(distSq, i);
            }
        }

        if (candidateTargets.empty()) {
            continue;
        }

        const auto targetMode = tower.targetingMode;
        if (targetMode == playlevel::TowerTargetingMode::Random) {
            static thread_local std::mt19937 rng{std::random_device{}()};
            std::shuffle(candidateTargets.begin(), candidateTargets.end(), rng);
        } else {
            std::sort(candidateTargets.begin(), candidateTargets.end(),
                      [&activeEnemies, targetMode](const std::pair<float, int>& a, const std::pair<float, int>& b) {
                          const playlevel::ActiveEnemy& enemyA = activeEnemies[static_cast<std::size_t>(a.second)];
                          const playlevel::ActiveEnemy& enemyB = activeEnemies[static_cast<std::size_t>(b.second)];

                          switch (targetMode) {
                          case playlevel::TowerTargetingMode::First:
                              if (enemyA.distanceAlongPath != enemyB.distanceAlongPath) {
                                  return enemyA.distanceAlongPath > enemyB.distanceAlongPath;
                              }
                              break;
                          case playlevel::TowerTargetingMode::Last:
                              if (enemyA.distanceAlongPath != enemyB.distanceAlongPath) {
                                  return enemyA.distanceAlongPath < enemyB.distanceAlongPath;
                              }
                              break;
                          case playlevel::TowerTargetingMode::HighestHp:
                              if (enemyA.health != enemyB.health) {
                                  return enemyA.health > enemyB.health;
                              }
                              break;
                          case playlevel::TowerTargetingMode::LowestHp:
                              if (enemyA.health != enemyB.health) {
                                  return enemyA.health < enemyB.health;
                              }
                              break;
                          case playlevel::TowerTargetingMode::Nearest:
                              break;
                          case playlevel::TowerTargetingMode::Random:
                              break;
                          }

                          if (a.first != b.first) {
                              return a.first < b.first;
                          }
                          return a.second < b.second;
                      });
        }

        const int projectileCount = std::max(1, tower.projectileCount);
        for (int shotIdx = 0; shotIdx < projectileCount; ++shotIdx) {
            int targetEnemyIndex = candidateTargets.front().second;
            if (shotIdx < static_cast<int>(candidateTargets.size())) {
                targetEnemyIndex = candidateTargets[static_cast<std::size_t>(shotIdx)].second;
            }

            const playlevel::ActiveEnemy& targetEnemy = activeEnemies[static_cast<std::size_t>(targetEnemyIndex)];
            const glm::vec3 launchPosition = tower.position + glm::vec3(0.0f, 0.7f, 0.0f);
            const glm::vec3 targetPosition =
                sampleRoutePosition(targetEnemy.distanceAlongPath) + glm::vec3(0.0f, 0.4f, 0.0f);

            glm::vec3 direction = targetPosition - launchPosition;
            const float dirLenSq = glm::dot(direction, direction);
            if (dirLenSq <= 1e-8f) {
                direction = glm::vec3(0.0f, 0.0f, 1.0f);
            } else {
                direction = glm::normalize(direction);
            }

            playlevel::ActiveProjectile projectile;
            projectile.towerId = tower.towerId;
            projectile.prototypeIndex = tower.projectilePrototypeIndex;
            projectile.position = launchPosition;
            projectile.velocity = direction * std::max(0.1f, tower.projectileSpeed);
            projectile.damage = std::max(0.01f, tower.attackDamage);
            projectile.armorPiercing = std::max(0.0f, tower.armorPiercing);
            projectile.damageType = tower.damageType;
            projectile.remainingLifeSeconds = kProjectileLifetimeSeconds;
            projectile.splashRadius = std::max(0.0f, tower.splashRadius);
            projectile.chainRange = std::max(0.1f, tower.chainRange);
            projectile.ricochetRange = std::max(0.1f, tower.ricochetRange);
            projectile.chainTargetCount = std::max(1, tower.chainTargetCount);
            projectile.remainingRicochetCount = std::max(0, tower.ricochetCount);
            projectile.sourceTowerPoolIndex = towerIndex;
            projectile.targetEnemyRuntimeId = targetEnemy.runtimeId;
            projectile.lastHitEnemyRuntimeId = 0;
            activeProjectiles.push_back(std::move(projectile));
        }

        tower.attackCooldownRemainingSeconds = std::max(0.01f, tower.attackIntervalSeconds);
    }
}

void PlayLevelCombatController::updateProjectiles(float dt,
                                                  const std::function<glm::vec3(float)>& sampleRoutePosition,
                                                  std::vector<playlevel::PlacedTower>& placedTowers,
                                                  std::vector<playlevel::ActiveEnemy>& activeEnemies,
                                                  std::vector<playlevel::ActiveProjectile>& activeProjectiles) const {
    std::size_t projectileWriteIndex = 0;
    for (std::size_t i = 0; i < activeProjectiles.size(); ++i) {
        playlevel::ActiveProjectile projectile = activeProjectiles[i];

                                                    int targetEnemyIndex = -1;
                                                    for (int enemyIdx = 0; enemyIdx < static_cast<int>(activeEnemies.size()); ++enemyIdx) {
                                                        if (activeEnemies[static_cast<std::size_t>(enemyIdx)].runtimeId == projectile.targetEnemyRuntimeId) {
                                                            targetEnemyIndex = enemyIdx;
                                                            break;
                                                        }
                                                    }

                                                    if (targetEnemyIndex < 0) {
            continue;
        }

                                                    playlevel::ActiveEnemy& targetEnemy = activeEnemies[static_cast<std::size_t>(targetEnemyIndex)];

                                                    const glm::vec3 enemyPos = sampleRoutePosition(targetEnemy.distanceAlongPath) + glm::vec3(0.0f, 0.4f, 0.0f);
        glm::vec3 toEnemy = enemyPos - projectile.position;
        const float distanceToEnemy = glm::length(toEnemy);

        if (distanceToEnemy > 1e-6f) {
            toEnemy /= distanceToEnemy;
        } else {
            toEnemy = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        const float projectileSpeed = glm::length(projectile.velocity);
        projectile.velocity = toEnemy * std::max(0.1f, projectileSpeed);
        projectile.position += projectile.velocity * dt;
        projectile.remainingLifeSeconds -= dt;

        const float travelThisFrame = std::max(0.0f, glm::length(projectile.velocity) * dt);
        const glm::vec3 postMoveToEnemy = enemyPos - projectile.position;
        const float postMoveDistanceSq = glm::dot(postMoveToEnemy, postMoveToEnemy);
        const bool reachedTarget = postMoveDistanceSq <= (kProjectileHitRadius * kProjectileHitRadius) ||
                                   distanceToEnemy <= (travelThisFrame + kProjectileHitRadius);

        if (reachedTarget) {
            std::unordered_set<std::uint64_t> hitEnemyIds;
            std::vector<int> hitEnemyIndices;
            hitEnemyIndices.reserve(8);

            auto markHit = [&](int enemyIdx) {
                if (enemyIdx < 0 || enemyIdx >= static_cast<int>(activeEnemies.size())) {
                    return;
                }
                playlevel::ActiveEnemy& enemy = activeEnemies[static_cast<std::size_t>(enemyIdx)];
                if (enemy.health <= 0.0f) {
                    return;
                }
                if (!hitEnemyIds.insert(enemy.runtimeId).second) {
                    return;
                }
                hitEnemyIndices.push_back(enemyIdx);
            };

            markHit(targetEnemyIndex);

            if (projectile.chainTargetCount > 1) {
                const float chainRangeSq = projectile.chainRange * projectile.chainRange;
                for (int chainStep = 1; chainStep < projectile.chainTargetCount; ++chainStep) {
                    int bestIdx = -1;
                    float bestDistSq = std::numeric_limits<float>::max();

                    for (int enemyIdx = 0; enemyIdx < static_cast<int>(activeEnemies.size()); ++enemyIdx) {
                        playlevel::ActiveEnemy& enemy = activeEnemies[static_cast<std::size_t>(enemyIdx)];
                        if (enemy.health <= 0.0f || hitEnemyIds.find(enemy.runtimeId) != hitEnemyIds.end()) {
                            continue;
                        }

                        const glm::vec3 enemyWorld =
                            sampleRoutePosition(enemy.distanceAlongPath) + glm::vec3(0.0f, 0.4f, 0.0f);
                        const glm::vec3 delta = enemyWorld - enemyPos;
                        const float distSq = glm::dot(delta, delta);
                        if (distSq <= chainRangeSq && distSq < bestDistSq) {
                            bestDistSq = distSq;
                            bestIdx = enemyIdx;
                        }
                    }

                    if (bestIdx < 0) {
                        break;
                    }
                    markHit(bestIdx);
                }
            }

            if (projectile.splashRadius > 0.0f) {
                const float splashRangeSq = projectile.splashRadius * projectile.splashRadius;
                for (int enemyIdx = 0; enemyIdx < static_cast<int>(activeEnemies.size()); ++enemyIdx) {
                    playlevel::ActiveEnemy& enemy = activeEnemies[static_cast<std::size_t>(enemyIdx)];
                    if (enemy.health <= 0.0f || hitEnemyIds.find(enemy.runtimeId) != hitEnemyIds.end()) {
                        continue;
                    }

                    const glm::vec3 enemyWorld =
                        sampleRoutePosition(enemy.distanceAlongPath) + glm::vec3(0.0f, 0.4f, 0.0f);
                    const glm::vec3 delta = enemyWorld - enemyPos;
                    const float distSq = glm::dot(delta, delta);
                    if (distSq <= splashRangeSq) {
                        markHit(enemyIdx);
                    }
                }
            }

            for (int enemyIdx : hitEnemyIndices) {
                playlevel::ActiveEnemy& hitEnemy = activeEnemies[static_cast<std::size_t>(enemyIdx)];
                float resistancePercent = 0.0f;
                const auto resistanceIt = hitEnemy.resistances.find(projectile.damageType);
                if (resistanceIt != hitEnemy.resistances.end()) {
                    resistancePercent = resistanceIt->second;
                }

                const float delta =
                    computeDamageDelta(projectile.damage, hitEnemy.armor, projectile.armorPiercing, resistancePercent);
                hitEnemy.health -= delta;
                hitEnemy.health = std::min(hitEnemy.maxHealth, hitEnemy.health);

                if (projectile.sourceTowerPoolIndex >= 0 &&
                    projectile.sourceTowerPoolIndex < static_cast<int>(placedTowers.size())) {
                    playlevel::PlacedTower& sourceTower =
                        placedTowers[static_cast<std::size_t>(projectile.sourceTowerPoolIndex)];
                    sourceTower.totalDamageDealt += std::max(0.0f, delta);
                }
            }

            if (projectile.remainingRicochetCount > 0 && projectile.remainingLifeSeconds > 0.0f) {
                const float ricochetRangeSq = projectile.ricochetRange * projectile.ricochetRange;
                int nextEnemyIdx = -1;
                float bestDistSq = std::numeric_limits<float>::max();

                for (int enemyIdx = 0; enemyIdx < static_cast<int>(activeEnemies.size()); ++enemyIdx) {
                    playlevel::ActiveEnemy& enemy = activeEnemies[static_cast<std::size_t>(enemyIdx)];
                    if (enemy.health <= 0.0f || hitEnemyIds.find(enemy.runtimeId) != hitEnemyIds.end() ||
                        enemy.runtimeId == projectile.lastHitEnemyRuntimeId) {
                        continue;
                    }

                    const glm::vec3 enemyWorld =
                        sampleRoutePosition(enemy.distanceAlongPath) + glm::vec3(0.0f, 0.4f, 0.0f);
                    const glm::vec3 delta = enemyWorld - enemyPos;
                    const float distSq = glm::dot(delta, delta);
                    if (distSq <= ricochetRangeSq && distSq < bestDistSq) {
                        bestDistSq = distSq;
                        nextEnemyIdx = enemyIdx;
                    }
                }

                if (nextEnemyIdx >= 0) {
                    projectile.lastHitEnemyRuntimeId = targetEnemy.runtimeId;
                    projectile.targetEnemyRuntimeId = activeEnemies[static_cast<std::size_t>(nextEnemyIdx)].runtimeId;
                    projectile.remainingRicochetCount -= 1;
                    projectile.position = enemyPos;
                    activeProjectiles[projectileWriteIndex++] = std::move(projectile);
                }
            }
            continue;
        }

        if (projectile.remainingLifeSeconds <= 0.0f) {
            continue;
        }

        activeProjectiles[projectileWriteIndex++] = std::move(projectile);
    }
    activeProjectiles.resize(projectileWriteIndex);
}

void PlayLevelCombatController::collectDefeatedEnemies(std::vector<playlevel::ActiveEnemy>& activeEnemies,
                                                       const std::function<void(float)>& onRewardGranted,
                                                       const std::function<void()>& onEnemyDefeated) const {
    // Health hitting zero no longer removes the enemy on the spot: it flips Alive -> Dying (once)
    // so its Death clip can play out. advanceDyingEnemies() removes it once that clip finishes.
    for (playlevel::ActiveEnemy& enemy : activeEnemies) {
        if (enemy.lifecycleState == playlevel::EnemyLifecycleState::Alive && enemy.health <= 0.0f) {
            enemy.lifecycleState = playlevel::EnemyLifecycleState::Dying;
            enemy.deathElapsedSeconds = 0.0f;
            onRewardGranted(std::max(0.0f, enemy.rewardMoney));
            onEnemyDefeated();
        }
    }
}

void PlayLevelCombatController::advanceDyingEnemies(
    float dt, const std::function<float(const playlevel::ActiveEnemy&)>& lookupDeathClipDurationSeconds,
    std::vector<playlevel::ActiveEnemy>& activeEnemies) const {
    std::size_t writeIndex = 0;
    for (std::size_t i = 0; i < activeEnemies.size(); ++i) {
        playlevel::ActiveEnemy enemy = activeEnemies[i];
        if (enemy.lifecycleState == playlevel::EnemyLifecycleState::Dying) {
            enemy.deathElapsedSeconds += dt;
            // Looked up per-enemy (by its own archetype's deathClipName AND its own
            // templatePrototypeIndex/animator) rather than once for the whole frame -- different
            // archetypes can name/author Death clips of different lengths on different models.
            const float requiredSeconds =
                std::max(0.0f, lookupDeathClipDurationSeconds(enemy));
            if (enemy.deathElapsedSeconds >= requiredSeconds) {
                enemy.lifecycleState = playlevel::EnemyLifecycleState::Dead;
            }
        }

        if (enemy.lifecycleState == playlevel::EnemyLifecycleState::Dead) {
            continue;
        }

        activeEnemies[writeIndex++] = std::move(enemy);
    }
    activeEnemies.resize(writeIndex);
}
