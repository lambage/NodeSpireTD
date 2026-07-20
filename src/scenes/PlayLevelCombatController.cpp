#include "scenes/PlayLevelCombatController.hpp"

#include <algorithm>
#include <glm/geometric.hpp>
#include <limits>
#include <utility>

namespace {

constexpr float kProjectileHitRadius = 0.45f;
constexpr float kProjectileLifetimeSeconds = 2.0f;

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
        int targetEnemyIndex = -1;
        float bestDistSq = std::numeric_limits<float>::max();

        for (int i = 0; i < static_cast<int>(activeEnemies.size()); ++i) {
            const playlevel::ActiveEnemy& enemy = activeEnemies[static_cast<std::size_t>(i)];
            const glm::vec3 enemyPos = sampleRoutePosition(enemy.distanceAlongPath);
            const glm::vec3 delta = enemyPos - tower.position;
            const float distSq = glm::dot(delta, delta);
            if (distSq <= attackRangeSq && distSq < bestDistSq) {
                bestDistSq = distSq;
                targetEnemyIndex = i;
            }
        }

        if (targetEnemyIndex < 0) {
            continue;
        }

        const playlevel::ActiveEnemy& targetEnemy = activeEnemies[static_cast<std::size_t>(targetEnemyIndex)];
        const glm::vec3 launchPosition = tower.position + glm::vec3(0.0f, 0.7f, 0.0f);
        const glm::vec3 targetPosition = sampleRoutePosition(targetEnemy.distanceAlongPath) + glm::vec3(0.0f, 0.4f, 0.0f);

        glm::vec3 direction = targetPosition - launchPosition;
        const float dirLenSq = glm::dot(direction, direction);
        if (dirLenSq <= 1e-8f) {
            direction = glm::vec3(0.0f, 0.0f, 1.0f);
        } else {
            direction = glm::normalize(direction);
        }

        playlevel::ActiveProjectile projectile;
        projectile.towerId = tower.towerId;
        projectile.position = launchPosition;
        projectile.velocity = direction * std::max(0.1f, tower.projectileSpeed);
        projectile.damage = std::max(0.01f, tower.attackDamage);
        projectile.remainingLifeSeconds = kProjectileLifetimeSeconds;
        projectile.targetEnemyRuntimeId = targetEnemy.runtimeId;
        activeProjectiles.push_back(std::move(projectile));

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

        playlevel::ActiveEnemy* targetEnemy = nullptr;
        for (playlevel::ActiveEnemy& enemy : activeEnemies) {
            if (enemy.runtimeId == projectile.targetEnemyRuntimeId) {
                targetEnemy = &enemy;
                break;
            }
        }

        if (!targetEnemy) {
            continue;
        }

        const glm::vec3 enemyPos = sampleRoutePosition(targetEnemy->distanceAlongPath) + glm::vec3(0.0f, 0.4f, 0.0f);
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

        if (reachedTarget || projectile.remainingLifeSeconds <= 0.0f) {
            targetEnemy->health -= projectile.damage;
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
