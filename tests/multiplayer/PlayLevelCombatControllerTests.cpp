#include "scenes/PlayLevelCombatController.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(PlayLevelCombatController, CreditsTheOwnerOfTheFinalDamagingTower) {
    PlayLevelCombatController combat;
    std::vector<playlevel::PlacedTower> towers(1);
    towers[0].runtimeId = 42;

    std::vector<playlevel::ActiveEnemy> enemies(1);
    enemies[0].runtimeId = 101;
    enemies[0].health = 5.0f;
    enemies[0].maxHealth = 5.0f;
    enemies[0].rewardMoney = 30.0f;

    std::vector<playlevel::ActiveProjectile> projectiles(1);
    projectiles[0].damage = 5.0f;
    projectiles[0].sourceTowerPoolIndex = 0;
    projectiles[0].sourceTowerRuntimeId = 42;
    projectiles[0].targetEnemyRuntimeId = 101;
    projectiles[0].remainingLifeSeconds = 1.0f;

    combat.updateProjectiles(0.0f, [](float) { return glm::vec3(0.0f); }, towers, enemies, projectiles);

    float reward = 0.0f;
    multiplayer::TowerRuntimeId creditedTowerRuntimeId = 0;
    combat.collectDefeatedEnemies(
        enemies,
        [&reward, &creditedTowerRuntimeId](float grantedReward, multiplayer::TowerRuntimeId towerRuntimeId) {
            reward = grantedReward;
            creditedTowerRuntimeId = towerRuntimeId;
        },
        []() {});

    EXPECT_FLOAT_EQ(reward, 30.0f);
    EXPECT_EQ(creditedTowerRuntimeId, 42);
}

TEST(PlayLevelCombatController, AppliesCriticalDamageAndProjectileStatusEffects) {
    PlayLevelCombatController combat;
    std::vector<playlevel::PlacedTower> towers(1);
    towers[0].runtimeId = 42;

    std::vector<playlevel::ActiveEnemy> enemies(1);
    enemies[0].runtimeId = 101;
    enemies[0].health = 100.0f;
    enemies[0].maxHealth = 100.0f;

    std::vector<playlevel::ActiveProjectile> projectiles(1);
    projectiles[0].damage = 10.0f;
    projectiles[0].critChance = 1.0f;
    projectiles[0].critDamageMul = 2.0f;
    projectiles[0].burnDamagePerSecond = 4.0f;
    projectiles[0].burnDuration = 3.0f;
    projectiles[0].slowAmount = 0.25f;
    projectiles[0].slowDuration = 2.0f;
    projectiles[0].freezeChance = 1.0f;
    projectiles[0].freezeDuration = 0.75f;
    projectiles[0].sourceTowerPoolIndex = 0;
    projectiles[0].sourceTowerRuntimeId = 42;
    projectiles[0].targetEnemyRuntimeId = 101;
    projectiles[0].remainingLifeSeconds = 1.0f;

    combat.updateProjectiles(0.0f, [](float) { return glm::vec3(0.0f); }, towers, enemies, projectiles);

    EXPECT_FLOAT_EQ(enemies[0].health, 80.0f);
    EXPECT_FLOAT_EQ(enemies[0].burnDamagePerSecond, 4.0f);
    EXPECT_FLOAT_EQ(enemies[0].burnRemainingSeconds, 3.0f);
    EXPECT_FLOAT_EQ(enemies[0].slowAmount, 0.25f);
    EXPECT_FLOAT_EQ(enemies[0].slowRemainingSeconds, 2.0f);
    EXPECT_FLOAT_EQ(enemies[0].freezeRemainingSeconds, 0.75f);
    EXPECT_FLOAT_EQ(towers[0].totalDamageDealt, 20.0f);
}

TEST(PlayLevelCombatController, SlowAndFreezeModifyEnemyMovement) {
    PlayLevelCombatController combat;
    std::vector<playlevel::ActiveEnemy> enemies(2);
    enemies[0].moveSpeed = 10.0f;
    enemies[0].slowAmount = 0.25f;
    enemies[0].slowRemainingSeconds = 2.0f;
    enemies[1].moveSpeed = 10.0f;
    enemies[1].freezeRemainingSeconds = 1.0f;

    combat.advanceEnemies(1.0f, 100.0f, enemies, [](float) {});

    EXPECT_FLOAT_EQ(enemies[0].distanceAlongPath, 7.5f);
    EXPECT_FLOAT_EQ(enemies[1].distanceAlongPath, 0.0f);
}

TEST(PlayLevelCombatController, BurnDamageTicksAndCreditsItsSourceTower) {
    PlayLevelCombatController combat;
    std::vector<playlevel::PlacedTower> towers(1);
    towers[0].runtimeId = 42;

    std::vector<playlevel::ActiveEnemy> enemies(1);
    enemies[0].health = 100.0f;
    enemies[0].maxHealth = 100.0f;
    enemies[0].burnDamagePerSecond = 10.0f;
    enemies[0].burnRemainingSeconds = 2.0f;
    enemies[0].burnSourceTowerRuntimeId = 42;

    combat.updateEnemyStatusEffects(0.5f, towers, enemies);

    EXPECT_FLOAT_EQ(enemies[0].health, 95.0f);
    EXPECT_FLOAT_EQ(enemies[0].burnRemainingSeconds, 1.5f);
    EXPECT_EQ(enemies[0].lastDamagingTowerRuntimeId, 42);
    EXPECT_FLOAT_EQ(towers[0].totalDamageDealt, 5.0f);
}