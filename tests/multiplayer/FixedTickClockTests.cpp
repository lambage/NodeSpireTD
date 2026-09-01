#include "multiplayer/FixedTickClock.hpp"

#include <gtest/gtest.h>

TEST(FixedTickClock, AdvancesAtTheSameRateAcrossCommonFrameDurations) {
    multiplayer::FixedTickClock sixtyFpsClock;
    multiplayer::FixedTickClock thirtyFpsClock;

    std::uint32_t sixtyFpsTicks = 0;
    std::uint32_t thirtyFpsTicks = 0;
    for (int frame = 0; frame < 60; ++frame) {
        sixtyFpsTicks += sixtyFpsClock.consumeTicks(1.0f / 60.0f);
    }
    for (int frame = 0; frame < 30; ++frame) {
        thirtyFpsTicks += thirtyFpsClock.consumeTicks(1.0f / 30.0f);
    }

    EXPECT_EQ(sixtyFpsTicks, 30);
    EXPECT_EQ(thirtyFpsTicks, 30);
}

TEST(FixedTickClock, BoundsCatchUpWorkAfterALongFrame) {
    multiplayer::FixedTickClock clock;

    EXPECT_EQ(clock.consumeTicks(10.0f), multiplayer::FixedTickClock::kMaxTicksPerFrame);
    EXPECT_EQ(clock.consumeTicks(0.0f), 0);
}