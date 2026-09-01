#include "multiplayer/MatchProtocolAdapter.hpp"

#include "nodespire/multiplayer/v1/match.pb.h"

#include <limits>
#include <type_traits>

namespace multiplayer {
namespace {

using WireCommand = nodespire::multiplayer::v1::PlayerCommandRequest;
using WireTargetingMode = nodespire::multiplayer::v1::TowerTargetingMode;

std::optional<WireTargetingMode> toWireTargetingMode(TowerTargetingMode mode) {
    switch (mode) {
    case TowerTargetingMode::First:
        return nodespire::multiplayer::v1::TOWER_TARGETING_MODE_FIRST;
    case TowerTargetingMode::Last:
        return nodespire::multiplayer::v1::TOWER_TARGETING_MODE_LAST;
    case TowerTargetingMode::Nearest:
        return nodespire::multiplayer::v1::TOWER_TARGETING_MODE_NEAREST;
    case TowerTargetingMode::Random:
        return nodespire::multiplayer::v1::TOWER_TARGETING_MODE_RANDOM;
    case TowerTargetingMode::HighestHp:
        return nodespire::multiplayer::v1::TOWER_TARGETING_MODE_HIGHEST_HP;
    case TowerTargetingMode::LowestHp:
        return nodespire::multiplayer::v1::TOWER_TARGETING_MODE_LOWEST_HP;
    }
    return std::nullopt;
}

std::optional<TowerTargetingMode> fromWireTargetingMode(WireTargetingMode mode) {
    switch (mode) {
    case nodespire::multiplayer::v1::TOWER_TARGETING_MODE_FIRST:
        return TowerTargetingMode::First;
    case nodespire::multiplayer::v1::TOWER_TARGETING_MODE_LAST:
        return TowerTargetingMode::Last;
    case nodespire::multiplayer::v1::TOWER_TARGETING_MODE_NEAREST:
        return TowerTargetingMode::Nearest;
    case nodespire::multiplayer::v1::TOWER_TARGETING_MODE_RANDOM:
        return TowerTargetingMode::Random;
    case nodespire::multiplayer::v1::TOWER_TARGETING_MODE_HIGHEST_HP:
        return TowerTargetingMode::HighestHp;
    case nodespire::multiplayer::v1::TOWER_TARGETING_MODE_LOWEST_HP:
        return TowerTargetingMode::LowestHp;
    default:
        return std::nullopt;
    }
}

DecodedPlayerCommand rejectDecode(CommandDecodeError error) {
    return {.command = std::nullopt, .error = error};
}

} // namespace

std::optional<std::string> MatchProtocolAdapter::serializePlayerCommand(const PlayerCommandRequest& command) {
    WireCommand wireCommand;
    wireCommand.set_protocol_version(command.protocolVersion);
    wireCommand.set_player_id(command.playerId);
    wireCommand.set_sequence(command.sequence);

    const bool converted = std::visit(
        [&wireCommand](const auto& payload) {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, PlaceTowerCommand>) {
                auto* placeTower = wireCommand.mutable_place_tower();
                placeTower->set_tower_archetype_id(payload.towerArchetypeId);
                auto* position = placeTower->mutable_requested_position();
                position->set_x(payload.requestedPosition.x);
                position->set_y(payload.requestedPosition.y);
                position->set_z(payload.requestedPosition.z);
                return true;
            } else if constexpr (std::is_same_v<Payload, UpgradeTowerCommand>) {
                auto* upgradeTower = wireCommand.mutable_upgrade_tower();
                upgradeTower->set_tower_runtime_id(payload.towerRuntimeId);
                upgradeTower->set_upgrade_node_id(payload.upgradeNodeId);
                return true;
            } else if constexpr (std::is_same_v<Payload, SetTowerTargetingCommand>) {
                const auto targetingMode = toWireTargetingMode(payload.targetingMode);
                if (!targetingMode) {
                    return false;
                }
                auto* setTargeting = wireCommand.mutable_set_tower_targeting();
                setTargeting->set_tower_runtime_id(payload.towerRuntimeId);
                setTargeting->set_targeting_mode(*targetingMode);
                return true;
            } else {
                wireCommand.mutable_start_wave();
                return true;
            }
        },
        command.payload);

    if (!converted || wireCommand.ByteSizeLong() > kMaxPlayerCommandBytes) {
        return std::nullopt;
    }

    std::string bytes;
    return wireCommand.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

DecodedPlayerCommand MatchProtocolAdapter::decodePlayerCommand(std::string_view payload) {
    if (payload.size() > kMaxPlayerCommandBytes) {
        return rejectDecode(CommandDecodeError::PayloadTooLarge);
    }

    WireCommand wireCommand;
    if (!wireCommand.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        return rejectDecode(CommandDecodeError::MalformedPayload);
    }
    if (wireCommand.protocol_version() > std::numeric_limits<std::uint16_t>::max()) {
        return rejectDecode(CommandDecodeError::ProtocolVersionOutOfRange);
    }

    PlayerCommandRequest command;
    command.protocolVersion = static_cast<std::uint16_t>(wireCommand.protocol_version());
    command.playerId = wireCommand.player_id();
    command.sequence = wireCommand.sequence();

    switch (wireCommand.command_case()) {
    case WireCommand::kPlaceTower: {
        const auto& placeTower = wireCommand.place_tower();
        command.payload = PlaceTowerCommand{placeTower.tower_archetype_id(),
                                            {placeTower.requested_position().x(), placeTower.requested_position().y(),
                                             placeTower.requested_position().z()}};
        break;
    }
    case WireCommand::kUpgradeTower: {
        const auto& upgradeTower = wireCommand.upgrade_tower();
        command.payload = UpgradeTowerCommand{upgradeTower.tower_runtime_id(), upgradeTower.upgrade_node_id()};
        break;
    }
    case WireCommand::kSetTowerTargeting: {
        const auto& setTargeting = wireCommand.set_tower_targeting();
        const auto targetingMode = fromWireTargetingMode(setTargeting.targeting_mode());
        if (!targetingMode) {
            return rejectDecode(CommandDecodeError::InvalidTargetingMode);
        }
        command.payload = SetTowerTargetingCommand{setTargeting.tower_runtime_id(), *targetingMode};
        break;
    }
    case WireCommand::kStartWave:
        command.payload = StartWaveCommand{};
        break;
    case WireCommand::COMMAND_NOT_SET:
        return rejectDecode(CommandDecodeError::CommandNotSet);
    }

    return {.command = std::move(command), .error = CommandDecodeError::None};
}

} // namespace multiplayer