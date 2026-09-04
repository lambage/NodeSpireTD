#include "multiplayer/LocalMatchHost.hpp"
#include "multiplayer/MatchProtocolAdapter.hpp"

#include "nodespire/multiplayer/v1/match.pb.h"

#include <gtest/gtest.h>

namespace {

using multiplayer::ContentManifest;
using multiplayer::JoinMatchRequest;
using multiplayer::kMatchProtocolVersion;
using multiplayer::LocalMatchHost;
using multiplayer::MatchProtocolAdapter;

constexpr std::string_view kExpectedContentHash = "expected-content-hash";

std::string joinRequestBytes(std::uint16_t protocolVersion, std::string_view contentHash) {
    JoinMatchRequest request;
    request.protocolVersion = protocolVersion;
    request.playerDisplayName = "Player";
    request.contentManifest = ContentManifest{std::string(contentHash)};
    const auto serialized = MatchProtocolAdapter::serializeJoinMatchRequest(request);
    return serialized.value_or(std::string{});
}

} // namespace

TEST(LocalMatchHost, AcceptsJoinRequestAndAssignsAnUnusedPlayerId) {
    LocalMatchHost host;
    host.registerPlayer(1); // the local host player already occupies id 1

    const auto outcome =
        host.processJoinRequest(joinRequestBytes(kMatchProtocolVersion, kExpectedContentHash), kExpectedContentHash, 42);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_TRUE(outcome->acceptedPlayerId.has_value());
    EXPECT_EQ(*outcome->acceptedPlayerId, 2U);

    nodespire::multiplayer::v1::JoinMatchResult wireResult;
    ASSERT_TRUE(wireResult.ParseFromString(outcome->serializedResult));
    ASSERT_EQ(wireResult.result_case(), nodespire::multiplayer::v1::JoinMatchResult::kAccepted);
    EXPECT_EQ(wireResult.accepted().player_id(), 2U);
    EXPECT_EQ(wireResult.accepted().current_tick(), 42U);
}

TEST(LocalMatchHost, RejectsJoinRequestWithUnsupportedProtocolVersion) {
    LocalMatchHost host;

    const auto outcome = host.processJoinRequest(
        joinRequestBytes(static_cast<std::uint16_t>(kMatchProtocolVersion + 1), kExpectedContentHash),
        kExpectedContentHash, 0);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->acceptedPlayerId.has_value());

    nodespire::multiplayer::v1::JoinMatchResult wireResult;
    ASSERT_TRUE(wireResult.ParseFromString(outcome->serializedResult));
    ASSERT_EQ(wireResult.result_case(), nodespire::multiplayer::v1::JoinMatchResult::kRejected);
    EXPECT_EQ(wireResult.rejected().reason(),
             nodespire::multiplayer::v1::JoinMatchRejected::PROTOCOL_VERSION_UNSUPPORTED);
}

TEST(LocalMatchHost, RejectsJoinRequestWithMismatchedContentManifest) {
    LocalMatchHost host;

    const auto outcome =
        host.processJoinRequest(joinRequestBytes(kMatchProtocolVersion, "some-other-hash"), kExpectedContentHash, 0);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->acceptedPlayerId.has_value());

    nodespire::multiplayer::v1::JoinMatchResult wireResult;
    ASSERT_TRUE(wireResult.ParseFromString(outcome->serializedResult));
    ASSERT_EQ(wireResult.result_case(), nodespire::multiplayer::v1::JoinMatchResult::kRejected);
    EXPECT_EQ(wireResult.rejected().reason(), nodespire::multiplayer::v1::JoinMatchRejected::CONTENT_MANIFEST_MISMATCH);
}

TEST(LocalMatchHost, RejectsJoinRequestWhenMatchIsFull) {
    LocalMatchHost host(/*maxPlayers=*/1);
    host.registerPlayer(1);

    const auto outcome =
        host.processJoinRequest(joinRequestBytes(kMatchProtocolVersion, kExpectedContentHash), kExpectedContentHash, 0);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->acceptedPlayerId.has_value());

    nodespire::multiplayer::v1::JoinMatchResult wireResult;
    ASSERT_TRUE(wireResult.ParseFromString(outcome->serializedResult));
    ASSERT_EQ(wireResult.result_case(), nodespire::multiplayer::v1::JoinMatchResult::kRejected);
    EXPECT_EQ(wireResult.rejected().reason(), nodespire::multiplayer::v1::JoinMatchRejected::MATCH_FULL);
}

TEST(LocalMatchHost, ReleasingAPlayerFreesCapacityForANewJoin) {
    LocalMatchHost host(/*maxPlayers=*/1);
    host.registerPlayer(1);
    host.unregisterPlayer(1);

    const auto outcome =
        host.processJoinRequest(joinRequestBytes(kMatchProtocolVersion, kExpectedContentHash), kExpectedContentHash, 0);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_TRUE(outcome->acceptedPlayerId.has_value());
    EXPECT_EQ(*outcome->acceptedPlayerId, 2U);
}

TEST(LocalMatchHost, RejectsMalformedJoinRequestPayload) {
    LocalMatchHost host;

    const auto outcome = host.processJoinRequest("not a valid protobuf payload", kExpectedContentHash, 0);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->acceptedPlayerId.has_value());

    nodespire::multiplayer::v1::JoinMatchResult wireResult;
    ASSERT_TRUE(wireResult.ParseFromString(outcome->serializedResult));
    ASSERT_EQ(wireResult.result_case(), nodespire::multiplayer::v1::JoinMatchResult::kRejected);
    EXPECT_EQ(wireResult.rejected().reason(), nodespire::multiplayer::v1::JoinMatchRejected::REASON_UNSPECIFIED);
}
