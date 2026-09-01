#include "multiplayer/LoopbackMatchSession.hpp"
#include "multiplayer/MatchProtocolAdapter.hpp"

#include "nodespire/multiplayer/v1/match.pb.h"

#include <gtest/gtest.h>

namespace {

multiplayer::PlayerCommandRequest startWaveCommand(multiplayer::PlayerId playerId) {
    multiplayer::PlayerCommandRequest command;
    command.playerId = playerId;
    command.sequence = 1;
    command.payload = multiplayer::StartWaveCommand{};
    return command;
}

} // namespace

TEST(LoopbackMatchSession, RoutesCommandsToAuthorityAndPublishesSnapshots) {
    multiplayer::LoopbackMatchSession session;
    const auto peer = session.connectPlayer(7, 250.0f);
    ASSERT_TRUE(peer.has_value());
    ASSERT_TRUE(session.transport().consumeLatestSnapshot(*peer).has_value());

    const auto commandBytes = multiplayer::MatchProtocolAdapter::serializePlayerCommand(startWaveCommand(7));
    ASSERT_TRUE(commandBytes.has_value());
    ASSERT_TRUE(session.transport().sendClientCommand(*peer, *commandBytes));
    int commandCount = 0;
    session.advance(multiplayer::FixedTickClock::kTickSeconds,
                    [&commandCount](const multiplayer::PlayerCommandRequest&) {
                        ++commandCount;
                        return std::optional<multiplayer::CommandRejectionReason>{};
                    });
    EXPECT_EQ(commandCount, 1);

    const auto results = session.transport().drainCommandResults(*peer);
    ASSERT_EQ(results.size(), 1U);
    nodespire::multiplayer::v1::PlayerCommandResult result;
    ASSERT_TRUE(result.ParseFromString(results[0]));
    EXPECT_EQ(result.result_case(), nodespire::multiplayer::v1::PlayerCommandResult::kAccepted);

    session.advance(multiplayer::FixedTickClock::kTickSeconds * 2.0f, {});
    EXPECT_TRUE(session.transport().consumeLatestSnapshot(*peer).has_value());
}

TEST(LoopbackMatchSession, RejectsCommandsClaimingAnotherPlayersIdentity) {
    multiplayer::LoopbackMatchSession session;
    const auto peer = session.connectPlayer(7, 250.0f);
    ASSERT_TRUE(peer.has_value());
    const auto commandBytes = multiplayer::MatchProtocolAdapter::serializePlayerCommand(startWaveCommand(8));
    ASSERT_TRUE(commandBytes.has_value());
    ASSERT_TRUE(session.transport().sendClientCommand(*peer, *commandBytes));

    int commandCount = 0;
    session.advance(multiplayer::FixedTickClock::kTickSeconds,
                    [&commandCount](const multiplayer::PlayerCommandRequest&) {
                        ++commandCount;
                        return std::optional<multiplayer::CommandRejectionReason>{};
                    });
    EXPECT_EQ(commandCount, 0);

    const auto results = session.transport().drainCommandResults(*peer);
    ASSERT_EQ(results.size(), 1U);
    nodespire::multiplayer::v1::PlayerCommandResult result;
    ASSERT_TRUE(result.ParseFromString(results[0]));
    ASSERT_EQ(result.result_case(), nodespire::multiplayer::v1::PlayerCommandResult::kRejected);
    EXPECT_EQ(result.rejected().reason(), nodespire::multiplayer::v1::PlayerCommandRejected::UNKNOWN_PLAYER);
}