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