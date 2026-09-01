#include "multiplayer/LocalHostCommandGate.hpp"
#include "multiplayer/LocalMatchHost.hpp"
#include "multiplayer/MatchProtocolAdapter.hpp"

#include "nodespire/multiplayer/v1/match.pb.h"

#include <cassert>
#include <gtest/gtest.h>
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

multiplayer::PlayerCommandRequest targetingCommand(multiplayer::PlayerId playerId,
                                                    multiplayer::CommandSequence sequence,
                                                    multiplayer::TowerRuntimeId towerRuntimeId) {
    multiplayer::PlayerCommandRequest command;
    command.playerId = playerId;
    command.sequence = sequence;
    command.payload = multiplayer::SetTowerTargetingCommand{towerRuntimeId, multiplayer::TowerTargetingMode::Nearest};
    return command;
}

multiplayer::PlayerCommandRequest upgradeCommand(multiplayer::PlayerId playerId,
                                                  multiplayer::CommandSequence sequence,
                                                  multiplayer::TowerRuntimeId towerRuntimeId) {
    multiplayer::PlayerCommandRequest command;
    command.playerId = playerId;
    command.sequence = sequence;
    command.payload = multiplayer::UpgradeTowerCommand{towerRuntimeId, "quickdraw_rig"};
    return command;
}

multiplayer::PlayerCommandRequest placementCommand(multiplayer::PlayerId playerId,
                                                    multiplayer::CommandSequence sequence) {
    multiplayer::PlayerCommandRequest command;
    command.playerId = playerId;
    command.sequence = sequence;
    command.payload = multiplayer::PlaceTowerCommand{"archer_hut", {1.0f, 2.0f, 3.0f}};
    return command;
}

nodespire::multiplayer::v1::PlayerCommandResult parseResult(const std::optional<std::string>& bytes) {
    assert(bytes.has_value());
    nodespire::multiplayer::v1::PlayerCommandResult result;
    assert(result.ParseFromString(*bytes));
    return result;
}

TEST(MatchProtocolAdapter, RoundTripsStartWaveCommand) {
    const auto encoded = multiplayer::MatchProtocolAdapter::serializePlayerCommand(startWaveCommand(7, 3));
    assert(encoded.has_value());

    const auto decoded = multiplayer::MatchProtocolAdapter::decodePlayerCommand(*encoded);
    assert(decoded.error == multiplayer::CommandDecodeError::None);
    assert(decoded.command.has_value());
    assert(decoded.command->playerId == 7);
    assert(decoded.command->sequence == 3);
    assert(std::holds_alternative<multiplayer::StartWaveCommand>(decoded.command->payload));
}

TEST(MatchProtocolAdapter, RejectsMissingCommand) {
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

TEST(MatchProtocolAdapter, RejectsOversizedProtocolVersion) {
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

TEST(MatchProtocolAdapter, RejectsInvalidTargetingMode) {
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

TEST(LocalHostCommandGate, RejectsUnknownPlayerAndReplay) {
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

TEST(LocalHostCommandGate, RejectsReplayOfRejectedCommand) {
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

TEST(LocalMatchHost, AppliesAcceptedCommandOnce) {
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

TEST(LocalMatchHost, RejectsInvalidAndUnknownPayloads) {
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

TEST(LocalMatchHost, EnforcesTowerOwnership) {
    constexpr multiplayer::PlayerId kTowerOwner = 7;
    constexpr multiplayer::TowerRuntimeId kTowerRuntimeId = 42;
    multiplayer::LocalMatchHost host;
    assert(host.registerPlayer(kTowerOwner));
    assert(host.registerPlayer(8));

    const auto ownedCommand = multiplayer::MatchProtocolAdapter::serializePlayerCommand(
        targetingCommand(kTowerOwner, 1, kTowerRuntimeId));
    const auto ownedResult = parseResult(host.processCommand(
        *ownedCommand, 11, [](const multiplayer::PlayerCommandRequest& receivedCommand) {
            const auto* targeting = std::get_if<multiplayer::SetTowerTargetingCommand>(&receivedCommand.payload);
            assert(targeting != nullptr);
            assert(targeting->towerRuntimeId == kTowerRuntimeId);
            assert(targeting->targetingMode == multiplayer::TowerTargetingMode::Nearest);
            return std::optional<multiplayer::CommandRejectionReason>{};
        }));
    assert(ownedResult.result_case() == nodespire::multiplayer::v1::PlayerCommandResult::kAccepted);

    const auto unownedCommand = multiplayer::MatchProtocolAdapter::serializePlayerCommand(targetingCommand(8, 1, kTowerRuntimeId));
    const auto unownedResult = parseResult(host.processCommand(
        *unownedCommand, 12, [](const multiplayer::PlayerCommandRequest&) {
            return std::optional{multiplayer::CommandRejectionReason::TowerNotOwnedByPlayer};
        }));
    assert(unownedResult.result_case() == nodespire::multiplayer::v1::PlayerCommandResult::kRejected);
    assert(unownedResult.rejected().reason() ==
           nodespire::multiplayer::v1::PlayerCommandRejected::TOWER_NOT_OWNED_BY_PLAYER);
}

TEST(LocalMatchHost, ReceivesOnlyTowerUpgradeIntent) {
    multiplayer::LocalMatchHost host;
    assert(host.registerPlayer(7));
    const auto encoded = multiplayer::MatchProtocolAdapter::serializePlayerCommand(upgradeCommand(7, 1, 42));

    const auto wireResult = parseResult(host.processCommand(
        *encoded, 11, [](const multiplayer::PlayerCommandRequest& receivedCommand) {
            const auto* upgrade = std::get_if<multiplayer::UpgradeTowerCommand>(&receivedCommand.payload);
            assert(upgrade != nullptr);
            assert(upgrade->towerRuntimeId == 42);
            assert(upgrade->upgradeNodeId == "quickdraw_rig");
            return std::optional<multiplayer::CommandRejectionReason>{};
        }));
    assert(wireResult.result_case() == nodespire::multiplayer::v1::PlayerCommandResult::kAccepted);
}

TEST(LocalMatchHost, ReceivesOnlyTowerPlacementIntent) {
    multiplayer::LocalMatchHost host;
    assert(host.registerPlayer(7));
    const auto encoded = multiplayer::MatchProtocolAdapter::serializePlayerCommand(placementCommand(7, 1));

    const auto wireResult = parseResult(host.processCommand(
        *encoded, 11, [](const multiplayer::PlayerCommandRequest& receivedCommand) {
            const auto* placement = std::get_if<multiplayer::PlaceTowerCommand>(&receivedCommand.payload);
            assert(placement != nullptr);
            assert(placement->towerArchetypeId == "archer_hut");
            assert(placement->requestedPosition.x == 1.0f);
            assert(placement->requestedPosition.y == 2.0f);
            assert(placement->requestedPosition.z == 3.0f);
            return std::optional<multiplayer::CommandRejectionReason>{};
        }));
    assert(wireResult.result_case() == nodespire::multiplayer::v1::PlayerCommandResult::kAccepted);
}

} // namespace