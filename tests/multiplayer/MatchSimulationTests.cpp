#include "multiplayer/MatchSimulation.hpp"

#include <gtest/gtest.h>

TEST(MatchSimulation, OwnsPerPlayerBalancesAndFixedTickProgress) {
    multiplayer::MatchSimulation simulation;
    simulation.reset();
    ASSERT_TRUE(simulation.registerPlayer(7, 250.0f));
    ASSERT_TRUE(simulation.registerPlayer(8, 100.0f));

    ASSERT_TRUE(simulation.debitPlayer(7, 50.0f));
    ASSERT_TRUE(simulation.creditPlayer(8, 25.0f));
    EXPECT_FLOAT_EQ(simulation.playerBalance(7), 200.0f);
    EXPECT_FLOAT_EQ(simulation.playerBalance(8), 125.0f);

    multiplayer::SimulationTick observedTick = 0;
    simulation.advance(multiplayer::FixedTickClock::kTickSeconds,
                       [&observedTick](multiplayer::SimulationTick tick, float elapsedSeconds) {
                           observedTick = tick;
                           EXPECT_FLOAT_EQ(elapsedSeconds, multiplayer::FixedTickClock::kTickSeconds);
                       });
    EXPECT_EQ(observedTick, 1);
    EXPECT_EQ(simulation.currentTick(), 1);
}

TEST(MatchSimulation, OwnsAndResetsRuntimeEntityStorage) {
    multiplayer::MatchSimulation simulation;
    simulation.placedTowers().push_back({});
    simulation.activeProjectiles().push_back({});
    simulation.activeEnemies().push_back({});
    simulation.nextTowerRuntimeId() = 42;
    simulation.nextEnemyRuntimeId() = 99;
    simulation.nextProjectileRuntimeId() = 123;

    simulation.reset();

    EXPECT_TRUE(simulation.placedTowers().empty());
    EXPECT_TRUE(simulation.activeProjectiles().empty());
    EXPECT_TRUE(simulation.activeEnemies().empty());
    EXPECT_EQ(simulation.nextTowerRuntimeId(), 1);
    EXPECT_EQ(simulation.nextEnemyRuntimeId(), 1);
    EXPECT_EQ(simulation.nextProjectileRuntimeId(), 1);
}

TEST(MatchSimulation, OwnsAndResetsMatchAndWaveState) {
    multiplayer::MatchSimulation simulation;
    simulation.gameplayState().baseHealth = 20.0f;
    simulation.gameplayState().currentWave = 5;
    simulation.waveController().definitionsMutable().push_back({});

    simulation.reset();

    EXPECT_FLOAT_EQ(simulation.gameplayState().baseHealth, 100.0f);
    EXPECT_EQ(simulation.gameplayState().currentWave, 1);
    EXPECT_EQ(simulation.waveController().waveCount(), 0U);
}