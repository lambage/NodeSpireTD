#include "scenes/PlayLevelCombatController.hpp"

#include <algorithm>
#include <glm/geometric.hpp>
#include <limits>
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

void PlayLevelCombatController::advanceEnemies(float dt,
                                               float routeTotalLength,
                                               std::vector<playlevel::ActiveEnemy>& activeEnemies,
                                               const std::function<void(float)>& onEnemyReachedBase) const {
    std::size_t writeIndex = 0;
    for (std::size_t i = 0; i < activeEnemies.size(); ++i) {
        playlevel::ActiveEnemy enemy = activeEnemies[i];
        enemy.distanceAlongPath += std::max(0.05f, enemy.moveSpeed) * dt;

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
    for (playlevel::PlacedTower& tower : placedTowers) {
        tower.attackCooldownRemainingSeconds = std::max(0.0f, tower.attackCooldownRemainingSeconds - dt);
        if (tower.attackCooldownRemainingSeconds > 0.0f) {
            continue;
        }

        const float attackRangeSq = tower.attackRange * tower.attackRange;
        std::vector<std::pair<float, int>> candidateTargets;
        candidateTargets.reserve(activeEnemies.size());

        for (int i = 0; i < static_cast<int>(activeEnemies.size()); ++i) {
            const playlevel::ActiveEnemy& enemy = activeEnemies[static_cast<std::size_t>(i)];
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

        std::sort(candidateTargets.begin(), candidateTargets.end(),
                  [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first < b.first; });

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
            projectile.targetEnemyRuntimeId = targetEnemy.runtimeId;
            projectile.lastHitEnemyRuntimeId = 0;
            activeProjectiles.push_back(std::move(projectile));
        }

        tower.attackCooldownRemainingSeconds = std::max(0.01f, tower.attackIntervalSeconds);
    }
}

void PlayLevelCombatController::updateProjectiles(float dt,
                                                  const std::function<glm::vec3(float)>& sampleRoutePosition,
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
    std::size_t enemyWriteIndex = 0;
    for (std::size_t i = 0; i < activeEnemies.size(); ++i) {
        playlevel::ActiveEnemy enemy = activeEnemies[i];
        if (enemy.health <= 0.0f) {
            onRewardGranted(std::max(0.0f, enemy.rewardMoney));
            onEnemyDefeated();
            continue;
        }
        activeEnemies[enemyWriteIndex++] = std::move(enemy);
    }
    activeEnemies.resize(enemyWriteIndex);
}
