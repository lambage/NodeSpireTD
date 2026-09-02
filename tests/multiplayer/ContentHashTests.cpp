#include "multiplayer/ContentHash.hpp"

#include <gtest/gtest.h>

TEST(ContentHash, IsOrderIndependent) {
    const auto a = multiplayer::computeContentDigest({"goblin", "archer_tower", "cannon_tower"});
    const auto b = multiplayer::computeContentDigest({"cannon_tower", "goblin", "archer_tower"});
    EXPECT_EQ(a, b);
}

TEST(ContentHash, DiffersWhenContentDiffers) {
    const auto a = multiplayer::computeContentDigest({"goblin", "archer_tower"});
    const auto b = multiplayer::computeContentDigest({"goblin", "archer_tower", "cannon_tower"});
    EXPECT_NE(a, b);
}

TEST(ContentHash, DiffersWhenIdBoundariesShift) {
    // Concatenating without a separator would make {"ab","c"} collide with {"a","bc"}.
    const auto a = multiplayer::computeContentDigest({"ab", "c"});
    const auto b = multiplayer::computeContentDigest({"a", "bc"});
    EXPECT_NE(a, b);
}

TEST(ContentHash, EmptyInputIsStable) {
    EXPECT_EQ(multiplayer::computeContentDigest({}), multiplayer::computeContentDigest({}));
}
