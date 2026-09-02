#include "multiplayer/LanMatchClient.hpp"
#include "multiplayer/LanMatchTransport.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

// io_context::run_for() stops the context once its duration elapses even if a self-rearming
// read is still pending; restart() must run before the next pump or run_for() would no-op.
void pumpNetwork(boost::asio::io_context& ioContext, std::chrono::milliseconds duration) {
    ioContext.run_for(duration);
    if (ioContext.stopped()) {
        ioContext.restart();
    }
}

} // namespace

TEST(LanMatchTransport, DeliversCommandsResultsAndSnapshotsOverTcp) {
    boost::asio::io_context ioContext;

    multiplayer::LanMatchTransport host(ioContext);
    ASSERT_TRUE(host.listen(0));

    multiplayer::LanMatchClient client(ioContext);
    ASSERT_TRUE(client.connect("127.0.0.1", host.listenPort()));

    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto acceptedPeers = host.drainAcceptedPeers();
    ASSERT_EQ(acceptedPeers.size(), 1U);
    const auto peerId = acceptedPeers.front();

    ASSERT_TRUE(client.sendCommand("command-payload"));
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto commands = host.drainClientCommands();
    ASSERT_EQ(commands.size(), 1U);
    EXPECT_EQ(commands[0].peerId, peerId);
    EXPECT_EQ(commands[0].payload, "command-payload");

    ASSERT_TRUE(host.sendCommandResult(peerId, "result-payload"));
    host.publishSnapshot("snapshot-payload");
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto results = client.drainCommandResults();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0], "result-payload");

    const auto snapshot = client.consumeLatestSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(*snapshot, "snapshot-payload");
}

TEST(LanMatchTransport, DisconnectingAPeerStopsFurtherReceivesFromIt) {
    boost::asio::io_context ioContext;

    multiplayer::LanMatchTransport host(ioContext);
    ASSERT_TRUE(host.listen(0));

    multiplayer::LanMatchClient client(ioContext);
    ASSERT_TRUE(client.connect("127.0.0.1", host.listenPort()));
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto peerId = host.drainAcceptedPeers().front();
    ASSERT_TRUE(host.disconnectPeer(peerId));
    EXPECT_FALSE(host.disconnectPeer(peerId));
    EXPECT_FALSE(host.sendCommandResult(peerId, "unreachable"));
}
