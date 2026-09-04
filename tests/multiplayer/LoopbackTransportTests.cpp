#include "multiplayer/LoopbackTransport.hpp"

#include <gtest/gtest.h>

TEST(LoopbackTransport, DeliversReliableCommandsAndResultsToTheirPeer) {
    multiplayer::LoopbackTransport transport;
    const auto firstPeer = transport.connectClient();
    const auto secondPeer = transport.connectClient();

    ASSERT_TRUE(transport.sendClientCommand(secondPeer, "command"));
    const auto commands = transport.drainClientCommands();
    ASSERT_EQ(commands.size(), 1U);
    EXPECT_EQ(commands[0].peerId, secondPeer);
    EXPECT_EQ(commands[0].payload, "command");

    ASSERT_TRUE(transport.sendCommandResult(secondPeer, "result"));
    EXPECT_TRUE(transport.drainCommandResults(firstPeer).empty());
    const auto results = transport.drainCommandResults(secondPeer);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0], "result");
}

TEST(LoopbackTransport, KeepsOnlyTheNewestSnapshotForEachPeer) {
    multiplayer::LoopbackTransport transport;
    const auto peer = transport.connectClient();

    transport.publishSnapshot("snapshot-one");
    transport.publishSnapshot("snapshot-two");

    const auto snapshot = transport.consumeLatestSnapshot(peer);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(*snapshot, "snapshot-two");
    EXPECT_FALSE(transport.consumeLatestSnapshot(peer).has_value());
}