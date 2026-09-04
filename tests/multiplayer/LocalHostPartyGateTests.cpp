#include "multiplayer/LocalHostPartyGate.hpp"

#include <gtest/gtest.h>

namespace {

using multiplayer::LocalHostPartyGate;
using multiplayer::PartyJoinAccepted;
using multiplayer::PartyJoinRejected;
using multiplayer::PartyJoinRejectionReason;
using multiplayer::PartyJoinRequest;

PartyJoinRequest makeRequest(std::string displayName, std::string playerUuid = {}) {
    PartyJoinRequest request;
    request.displayName = std::move(displayName);
    request.playerUuid = std::move(playerUuid);
    return request;
}

} // namespace

TEST(LocalHostPartyGate, AcceptsJoinAndAddsMember) {
    LocalHostPartyGate gate(4);
    const auto result = gate.evaluateJoin(makeRequest("Alice", "uuid-alice"));
    ASSERT_TRUE(std::holds_alternative<PartyJoinAccepted>(result));

    const auto playerId = gate.addMember("Alice", /*isHost=*/false, "uuid-alice");
    EXPECT_EQ(gate.memberCount(), 1U);
    EXPECT_EQ(gate.displayNameForPlayer(playerId), "Alice");
}

TEST(LocalHostPartyGate, RejectsJoinBeyondCapacity) {
    LocalHostPartyGate gate(1);
    gate.addMember("Host", /*isHost=*/true, "uuid-host");

    const auto result = gate.evaluateJoin(makeRequest("Bob", "uuid-bob"));
    ASSERT_TRUE(std::holds_alternative<PartyJoinRejected>(result));
    EXPECT_EQ(std::get<PartyJoinRejected>(result).reason, PartyJoinRejectionReason::PartyFull);
}

TEST(LocalHostPartyGate, RejectsDuplicateActiveUuid) {
    LocalHostPartyGate gate(4);
    gate.addMember("Alice", /*isHost=*/false, "uuid-alice");

    const auto result = gate.evaluateJoin(makeRequest("Alice2", "uuid-alice"));
    ASSERT_TRUE(std::holds_alternative<PartyJoinRejected>(result));
    EXPECT_EQ(std::get<PartyJoinRejected>(result).reason, PartyJoinRejectionReason::AlreadyConnected);
}

TEST(LocalHostPartyGate, ReconnectingUuidReusesSamePlayerId) {
    LocalHostPartyGate gate(4);
    const auto firstJoinId = gate.addMember("Alice", /*isHost=*/false, "uuid-alice");

    // Simulate a dropped connection: the member is removed, but the uuid mapping survives.
    ASSERT_TRUE(gate.removeMember(firstJoinId));
    EXPECT_EQ(gate.memberCount(), 0U);

    const auto acceptResult = gate.evaluateJoin(makeRequest("Alice", "uuid-alice"));
    ASSERT_TRUE(std::holds_alternative<PartyJoinAccepted>(acceptResult));

    const auto reconnectedId = gate.addMember("Alice", /*isHost=*/false, "uuid-alice");
    EXPECT_EQ(reconnectedId, firstJoinId);
}

TEST(LocalHostPartyGate, DifferentUuidsGetDistinctPlayerIds) {
    LocalHostPartyGate gate(4);
    const auto aliceId = gate.addMember("Alice", /*isHost=*/false, "uuid-alice");
    const auto bobId = gate.addMember("Bob", /*isHost=*/false, "uuid-bob");
    EXPECT_NE(aliceId, bobId);
}
