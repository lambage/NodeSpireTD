#include "multiplayer/PlayerAccounts.hpp"

#include <gtest/gtest.h>

TEST(PlayerAccounts, KeepsBalancesIndependentByPlayer) {
    multiplayer::PlayerAccounts accounts;
    ASSERT_TRUE(accounts.registerPlayer(7, 250.0f));
    ASSERT_TRUE(accounts.registerPlayer(8, 150.0f));

    EXPECT_TRUE(accounts.debit(7, 100.0f));
    EXPECT_TRUE(accounts.credit(8, 25.0f));
    EXPECT_FLOAT_EQ(accounts.balance(7), 150.0f);
    EXPECT_FLOAT_EQ(accounts.balance(8), 175.0f);
}

TEST(PlayerAccounts, RejectsInvalidAndInsufficientTransactions) {
    multiplayer::PlayerAccounts accounts;
    ASSERT_TRUE(accounts.registerPlayer(7, 50.0f));

    EXPECT_FALSE(accounts.debit(7, 0.0f));
    EXPECT_FALSE(accounts.debit(7, 51.0f));
    EXPECT_FALSE(accounts.credit(7, 0.0f));
    EXPECT_FALSE(accounts.debit(8, 1.0f));
    EXPECT_FALSE(accounts.credit(8, 1.0f));
    EXPECT_FLOAT_EQ(accounts.balance(7), 50.0f);
}