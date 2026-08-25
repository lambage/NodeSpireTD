#include "scenes/EnemySpawnFactory.hpp"

playlevel::ActiveEnemy EnemySpawnFactory::create(const std::string& enemyId, const EnemyArchetype* archetype,
                                                 std::uint64_t runtimeId) {
    const float health = archetype ? archetype->health : 1.0f;
    const float shield = archetype ? archetype->shield : 0.0f;
    const float armor = archetype ? archetype->armor : 0.0f;
    const float moveSpeed = archetype ? archetype->moveSpeed : 1.0f;
    const float rewardMoney = archetype ? archetype->rewardMoney : 0.0f;
    const float renderScale = archetype ? archetype->renderScale : 1.0f;
    const float baseDamage = archetype ? archetype->baseDamage : 5.0f;
    const float facingYawOffsetDegrees = archetype ? archetype->facingYawOffsetDegrees : 0.0f;
    const auto resistances =
        archetype ? archetype->resistances : std::unordered_map<playlevel::DamageType, float, playlevel::DamageTypeHash>{};

    const float clampedHealth = std::max(1.0f, health);
    const float clampedShield = std::max(0.0f, shield);
    return playlevel::ActiveEnemy{enemyId,
                                  runtimeId,
                                  0.0f,
                                  clampedHealth,
                                  clampedHealth,
                                  clampedShield,
                                  clampedShield,
                                  std::max(0.0f, armor),
                                  resistances,
                                  std::max(0.05f, moveSpeed),
                                  std::max(0.0f, rewardMoney),
                                  std::max(1.0f, baseDamage),
                                  std::max(0.01f, renderScale),
                                  facingYawOffsetDegrees};
}
