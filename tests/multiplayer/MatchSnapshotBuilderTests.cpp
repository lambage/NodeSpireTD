#include "multiplayer/MatchSnapshotBuilder.hpp"

#include "nodespire/multiplayer/v1/match.pb.h"

#include <gtest/gtest.h>

TEST(MatchSnapshotBuilder, SerializesAuthoritativeStateInStableIdOrder) {
    multiplayer::MatchSimulation simulation;
    simulation.reset();
    ASSERT_TRUE(simulation.registerPlayer(8, 150.0f));
    ASSERT_TRUE(simulation.registerPlayer(7, 250.0f));
    simulation.gameplayState().matchStatus = MatchStatus::Running;
    simulation.gameplayState().baseHealth = 85.0f;
    simulation.gameplayState().currentWave = 3;
    simulation.gameplayState().waveInProgress = true;
    simulation.advance(multiplayer::FixedTickClock::kTickSeconds, [](multiplayer::SimulationTick, float) {});

    auto& secondTower = simulation.placedTowers().emplace_back();
    secondTower.runtimeId = 2;
    secondTower.ownerPlayerId = 8;
    secondTower.towerId = "archer_hut";
    secondTower.position = {2.0f, 0.0f, 3.0f};
    auto& firstTower = simulation.placedTowers().emplace_back();
    firstTower.runtimeId = 1;
    firstTower.ownerPlayerId = 7;
    firstTower.towerId = "archer_hut";
    firstTower.unlockedUpgradeNodeIds.push_back("quickdraw_rig");

    auto& enemy = simulation.activeEnemies().emplace_back();
    enemy.runtimeId = 101;
    enemy.enemyId = "goblin1";
    enemy.distanceAlongPath = 4.0f;
    enemy.health = 8.0f;
    auto& projectile = simulation.activeProjectiles().emplace_back();
    projectile.runtimeId = 201;
    projectile.towerId = "archer_hut";
    projectile.targetEnemyRuntimeId = 101;

    const auto bytes = multiplayer::MatchSnapshotBuilder::serialize(simulation);
    ASSERT_TRUE(bytes.has_value());
    nodespire::multiplayer::v1::MatchSnapshot snapshot;
    ASSERT_TRUE(snapshot.ParseFromString(*bytes));

    EXPECT_EQ(snapshot.simulation_tick(), 1U);
    EXPECT_EQ(snapshot.match_status(), nodespire::multiplayer::v1::MATCH_STATUS_RUNNING);
    ASSERT_EQ(snapshot.players_size(), 2);
    EXPECT_EQ(snapshot.players(0).player_id(), 7U);
    EXPECT_EQ(snapshot.players(1).player_id(), 8U);
    ASSERT_EQ(snapshot.towers_size(), 2);
    EXPECT_EQ(snapshot.towers(0).runtime_id(), 1U);
    EXPECT_EQ(snapshot.towers(0).owner_player_id(), 7U);
    EXPECT_EQ(snapshot.towers(1).runtime_id(), 2U);
    ASSERT_EQ(snapshot.enemies_size(), 1);
    EXPECT_EQ(snapshot.enemies(0).runtime_id(), 101U);
    ASSERT_EQ(snapshot.projectiles_size(), 1);
    EXPECT_EQ(snapshot.projectiles(0).runtime_id(), 201U);
    EXPECT_EQ(snapshot.projectiles(0).target_enemy_runtime_id(), 101U);
}