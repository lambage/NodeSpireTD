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