#include "multiplayer/LocalHostCommandGate.hpp"
#include "multiplayer/LocalMatchHost.hpp"
#include "multiplayer/MatchProtocolAdapter.hpp"

#include "nodespire/multiplayer/v1/match.pb.h"

#include <cassert>
#include <string>
#include <variant>

namespace {

multiplayer::PlayerCommandRequest startWaveCommand(multiplayer::PlayerId playerId, multiplayer::CommandSequence sequence) {
    multiplayer::PlayerCommandRequest command;
    command.playerId = playerId;
    command.sequence = sequence;
    command.payload = multiplayer::StartWaveCommand{};
    return command;
}

nodespire::multiplayer::v1::PlayerCommandResult parseResult(const std::optional<std::string>& bytes) {
    assert(bytes.has_value());
    nodespire::multiplayer::v1::PlayerCommandResult result;
    assert(result.ParseFromString(*bytes));
    return result;
}

void testRoundTrip() {
    const auto encoded = multiplayer::MatchProtocolAdapter::serializePlayerCommand(startWaveCommand(7, 3));
    assert(encoded.has_value());

    const auto decoded = multiplayer::MatchProtocolAdapter::decodePlayerCommand(*encoded);
    assert(decoded.error == multiplayer::CommandDecodeError::None);
    assert(decoded.command.has_value());
    assert(decoded.command->playerId == 7);
    assert(decoded.command->sequence == 3);
    assert(std::holds_alternative<multiplayer::StartWaveCommand>(decoded.command->payload));
}

void testMissingCommandIsRejected() {
    nodespire::multiplayer::v1::PlayerCommandRequest wireCommand;
    wireCommand.set_protocol_version(multiplayer::kMatchProtocolVersion);
    wireCommand.set_player_id(7);
    wireCommand.set_sequence(3);

    std::string bytes;
    assert(wireCommand.SerializeToString(&bytes));
    const auto decoded = multiplayer::MatchProtocolAdapter::decodePlayerCommand(bytes);
    assert(!decoded.command.has_value());
    assert(decoded.error == multiplayer::CommandDecodeError::CommandNotSet);
}

void testOversizedProtocolVersionIsRejected() {
    nodespire::multiplayer::v1::PlayerCommandRequest wireCommand;
    wireCommand.set_protocol_version(65536);
    wireCommand.set_player_id(7);
    wireCommand.set_sequence(3);
    wireCommand.mutable_start_wave();

    std::string bytes;
    assert(wireCommand.SerializeToString(&bytes));
    const auto decoded = multiplayer::MatchProtocolAdapter::decodePlayerCommand(bytes);
    assert(!decoded.command.has_value());
    assert(decoded.error == multiplayer::CommandDecodeError::ProtocolVersionOutOfRange);
}

void testInvalidTargetingModeIsRejected() {
    nodespire::multiplayer::v1::PlayerCommandRequest wireCommand;
    auto* targeting = wireCommand.mutable_set_tower_targeting();
    targeting->set_tower_runtime_id(42);
    targeting->set_targeting_mode(static_cast<nodespire::multiplayer::v1::TowerTargetingMode>(999));

    std::string bytes;
    assert(wireCommand.SerializeToString(&bytes));
    const auto decoded = multiplayer::MatchProtocolAdapter::decodePlayerCommand(bytes);
    assert(!decoded.command.has_value());
    assert(decoded.error == multiplayer::CommandDecodeError::InvalidTargetingMode);
}

void testHostRejectsUnknownAndReplay() {
    multiplayer::LocalHostCommandGate host;
    const auto unknownPlayerResult = host.validateAndApply(startWaveCommand(7, 1), 11, {});
    assert(std::holds_alternative<multiplayer::CommandRejected>(unknownPlayerResult));
    assert(std::get<multiplayer::CommandRejected>(unknownPlayerResult).reason ==
           multiplayer::CommandRejectionReason::UnknownPlayer);

    assert(host.registerPlayer(7));
    const auto acceptedResult = host.validateAndApply(startWaveCommand(7, 1), 11, {});
    assert(std::holds_alternative<multiplayer::CommandAccepted>(acceptedResult));

    const auto replayResult = host.validateAndApply(startWaveCommand(7, 1), 12, {});
    assert(std::holds_alternative<multiplayer::CommandRejected>(replayResult));
    assert(std::get<multiplayer::CommandRejected>(replayResult).reason ==
           multiplayer::CommandRejectionReason::DuplicateOrOutOfOrderSequence);
}

void testRejectedCommandCannotReplay() {
    multiplayer::LocalHostCommandGate host;
    assert(host.registerPlayer(7));

    const auto rejectedResult = host.validateAndApply(
        startWaveCommand(7, 1), 11,
        [](const multiplayer::PlayerCommandRequest&) { return multiplayer::CommandRejectionReason::WaveCannotStart; });
    assert(std::holds_alternative<multiplayer::CommandRejected>(rejectedResult));

    const auto replayResult = host.validateAndApply(startWaveCommand(7, 1), 12, {});
    assert(std::holds_alternative<multiplayer::CommandRejected>(replayResult));
    assert(std::get<multiplayer::CommandRejected>(replayResult).reason ==
           multiplayer::CommandRejectionReason::DuplicateOrOutOfOrderSequence);
}

void testLoopbackHostAppliesAcceptedCommandOnce() {
    multiplayer::LocalMatchHost host;
    assert(host.registerPlayer(7));
    const auto encoded = multiplayer::MatchProtocolAdapter::serializePlayerCommand(startWaveCommand(7, 1));
    int commandCount = 0;

    const auto response = host.processCommand(*encoded, 11, [&commandCount](const multiplayer::PlayerCommandRequest&) {
        ++commandCount;
        return std::optional<multiplayer::CommandRejectionReason>{};
    });
    const auto wireResult = parseResult(response);
    assert(wireResult.result_case() == nodespire::multiplayer::v1::PlayerCommandResult::kAccepted);
    assert(wireResult.accepted().player_id() == 7);
    assert(wireResult.accepted().sequence() == 1);
    assert(wireResult.accepted().applied_at_tick() == 11);
    assert(commandCount == 1);

    const auto replayResponse = host.processCommand(*encoded, 12, [&commandCount](const multiplayer::PlayerCommandRequest&) {
        ++commandCount;
        return std::optional<multiplayer::CommandRejectionReason>{};
    });
    const auto replayResult = parseResult(replayResponse);
    assert(replayResult.result_case() == nodespire::multiplayer::v1::PlayerCommandResult::kRejected);
    assert(replayResult.rejected().reason() ==
           nodespire::multiplayer::v1::PlayerCommandRejected::DUPLICATE_OR_OUT_OF_ORDER_SEQUENCE);
    assert(commandCount == 1);
}

void testLoopbackHostRejectsInvalidAndUnknownPayloads() {
    multiplayer::LocalMatchHost host;
    const auto malformedResult = parseResult(host.processCommand("not a protobuf command", 11, {}));
    assert(malformedResult.result_case() == nodespire::multiplayer::v1::PlayerCommandResult::kRejected);
    assert(malformedResult.rejected().reason() == nodespire::multiplayer::v1::PlayerCommandRejected::INVALID_PAYLOAD);

    const auto encoded = multiplayer::MatchProtocolAdapter::serializePlayerCommand(startWaveCommand(7, 1));
    const auto unknownPlayerResult = parseResult(host.processCommand(*encoded, 11, {}));
    assert(unknownPlayerResult.result_case() == nodespire::multiplayer::v1::PlayerCommandResult::kRejected);
    assert(unknownPlayerResult.rejected().player_id() == 7);
    assert(unknownPlayerResult.rejected().sequence() == 1);
    assert(unknownPlayerResult.rejected().reason() == nodespire::multiplayer::v1::PlayerCommandRejected::UNKNOWN_PLAYER);
}

} // namespace

int main() {
    testRoundTrip();
    testMissingCommandIsRejected();
    testOversizedProtocolVersionIsRejected();
    testInvalidTargetingModeIsRejected();
    testHostRejectsUnknownAndReplay();
    testRejectedCommandCannotReplay();
    testLoopbackHostAppliesAcceptedCommandOnce();
    testLoopbackHostRejectsInvalidAndUnknownPayloads();
}