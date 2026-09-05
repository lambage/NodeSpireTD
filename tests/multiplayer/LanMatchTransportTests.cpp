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

multiplayer::TransportPeerId connectAndJoin(boost::asio::io_context& ioContext, multiplayer::LanMatchTransport& host,
                                            multiplayer::LanMatchClient& client) {
    if (!client.connect("127.0.0.1", host.listenPort())) {
        return 0;
    }
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto acceptedPeers = host.drainAcceptedPeers();
    if (acceptedPeers.size() != 1) {
        return 0;
    }
    const auto peerId = acceptedPeers.front();

    if (!client.sendJoinRequest("join-payload")) {
        return 0;
    }
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto joinRequests = host.drainJoinRequests();
    if (joinRequests.size() != 1 || joinRequests.front().peerId != peerId ||
        joinRequests.front().payload != "join-payload") {
        return 0;
    }

    if (!host.markPeerJoined(peerId) || !host.sendJoinResult(peerId, "accepted-payload")) {
        return 0;
    }
    pumpNetwork(ioContext, std::chrono::milliseconds(200));
    return peerId;
}

} // namespace

TEST(LanMatchTransport, DeliversCommandsResultsAndSnapshotsAfterJoinHandshake) {
    boost::asio::io_context ioContext;

    multiplayer::LanMatchTransport host(ioContext);
    ASSERT_TRUE(host.listen(0));

    multiplayer::LanMatchClient client(ioContext);
    const auto peerId = connectAndJoin(ioContext, host, client);
    ASSERT_NE(peerId, 0U);

    const auto joinResult = client.consumeJoinResult();
    ASSERT_TRUE(joinResult.has_value());
    EXPECT_EQ(*joinResult, "accepted-payload");

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

TEST(LanMatchTransport, RejectsClientCommandsSentBeforeTheJoinHandshake) {
    boost::asio::io_context ioContext;

    multiplayer::LanMatchTransport host(ioContext);
    ASSERT_TRUE(host.listen(0));

    multiplayer::LanMatchClient client(ioContext);
    ASSERT_TRUE(client.connect("127.0.0.1", host.listenPort()));
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto peerId = host.drainAcceptedPeers().front();
    ASSERT_TRUE(client.sendCommand("too-early"));
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    EXPECT_TRUE(host.drainClientCommands().empty());
    // The host disconnects a peer that skips the handshake, so it can no longer be addressed.
    EXPECT_FALSE(host.sendCommandResult(peerId, "unreachable"));
}

TEST(LanMatchTransport, IgnoresSnapshotsAndResultsForPeersThatHaveNotJoined) {
    boost::asio::io_context ioContext;

    multiplayer::LanMatchTransport host(ioContext);
    ASSERT_TRUE(host.listen(0));

    multiplayer::LanMatchClient client(ioContext);
    ASSERT_TRUE(client.connect("127.0.0.1", host.listenPort()));
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto peerId = host.drainAcceptedPeers().front();
    EXPECT_FALSE(host.sendCommandResult(peerId, "not-joined-yet"));

    host.publishSnapshot("snapshot-payload");
    pumpNetwork(ioContext, std::chrono::milliseconds(200));
    EXPECT_FALSE(client.consumeLatestSnapshot().has_value());
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

TEST(LanMatchTransport, FlushesQueuedResultBeforeDisconnectingPeer) {
    boost::asio::io_context ioContext;

    multiplayer::LanMatchTransport host(ioContext);
    ASSERT_TRUE(host.listen(0));

    multiplayer::LanMatchClient client(ioContext);
    ASSERT_TRUE(client.connect("127.0.0.1", host.listenPort()));
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto acceptedPeers = host.drainAcceptedPeers();
    ASSERT_EQ(acceptedPeers.size(), 1U);
    const auto peerId = acceptedPeers.front();
    ASSERT_TRUE(host.sendPartyJoinResult(peerId, "rejected-payload"));
    ASSERT_TRUE(host.disconnectPeerAfterWrites(peerId));
    EXPECT_FALSE(host.sendPartyJoinResult(peerId, "unreachable"));

    pumpNetwork(ioContext, std::chrono::milliseconds(200));
    const auto result = client.consumePartyJoinResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "rejected-payload");
}

TEST(LanMatchTransport, SendsPartyChatMessageToOneJoinedPeer) {
    boost::asio::io_context ioContext;

    multiplayer::LanMatchTransport host(ioContext);
    ASSERT_TRUE(host.listen(0));

    multiplayer::LanMatchClient client(ioContext);
    ASSERT_TRUE(client.connect("127.0.0.1", host.listenPort()));
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto acceptedPeers = host.drainAcceptedPeers();
    ASSERT_EQ(acceptedPeers.size(), 1U);
    const auto peerId = acceptedPeers.front();
    EXPECT_FALSE(host.sendPartyChatMessage(peerId, "too-early"));

    ASSERT_TRUE(host.markPeerPartyJoined(peerId));
    ASSERT_TRUE(host.sendPartyChatMessage(peerId, "welcome-payload"));
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto messages = client.drainPartyChatMessages();
    ASSERT_EQ(messages.size(), 1U);
    EXPECT_EQ(messages.front(), "welcome-payload");
}

TEST(LanMatchTransport, BroadcastsMatchBeginOnlyToPartyJoinedPeers) {
    boost::asio::io_context ioContext;

    multiplayer::LanMatchTransport host(ioContext);
    ASSERT_TRUE(host.listen(0));

    multiplayer::LanMatchClient client(ioContext);
    ASSERT_TRUE(client.connect("127.0.0.1", host.listenPort()));
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto acceptedPeers = host.drainAcceptedPeers();
    ASSERT_EQ(acceptedPeers.size(), 1U);
    host.broadcastPartyMatchBegin();
    pumpNetwork(ioContext, std::chrono::milliseconds(200));
    EXPECT_FALSE(client.consumePartyMatchBegin());

    ASSERT_TRUE(host.markPeerPartyJoined(acceptedPeers.front()));
    host.broadcastPartyMatchBegin();
    pumpNetwork(ioContext, std::chrono::milliseconds(200));
    EXPECT_TRUE(client.consumePartyMatchBegin());
    EXPECT_FALSE(client.consumePartyMatchBegin());
}

TEST(LanMatchTransport, BroadcastsMatchEndOnlyToPartyJoinedPeers) {
    boost::asio::io_context ioContext;

    multiplayer::LanMatchTransport host(ioContext);
    ASSERT_TRUE(host.listen(0));

    multiplayer::LanMatchClient client(ioContext);
    ASSERT_TRUE(client.connect("127.0.0.1", host.listenPort()));
    pumpNetwork(ioContext, std::chrono::milliseconds(200));

    const auto acceptedPeers = host.drainAcceptedPeers();
    ASSERT_EQ(acceptedPeers.size(), 1U);
    host.broadcastPartyMatchEnd();
    pumpNetwork(ioContext, std::chrono::milliseconds(200));
    EXPECT_FALSE(client.consumePartyMatchEnd());

    ASSERT_TRUE(host.markPeerPartyJoined(acceptedPeers.front()));
    host.broadcastPartyMatchEnd();
    pumpNetwork(ioContext, std::chrono::milliseconds(200));
    EXPECT_TRUE(client.consumePartyMatchEnd());
    EXPECT_FALSE(client.consumePartyMatchEnd());
}

