#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace multiplayer {

inline constexpr std::uint16_t kMatchProtocolVersion = 1;

using PlayerId = std::uint64_t;
using TowerRuntimeId = std::uint64_t;
using CommandSequence = std::uint64_t;
using SimulationTick = std::uint64_t;

struct WorldPosition {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class TowerTargetingMode : std::uint8_t {
    First,
    Last,
    Nearest,
    Random,
    HighestHp,
    LowestHp,
};

struct PlaceTowerCommand {
    std::string towerArchetypeId;
    WorldPosition requestedPosition;
};

struct UpgradeTowerCommand {
    TowerRuntimeId towerRuntimeId = 0;
    std::string upgradeNodeId;
};

struct SetTowerTargetingCommand {
    TowerRuntimeId towerRuntimeId = 0;
    TowerTargetingMode targetingMode = TowerTargetingMode::First;
};

struct StartWaveCommand {};

using PlayerCommandPayload =
    std::variant<PlaceTowerCommand, UpgradeTowerCommand, SetTowerTargetingCommand, StartWaveCommand>;

// Clients send only this request. Costs, damage, rewards, and derived tower data stay host-only.
struct PlayerCommandRequest {
    std::uint16_t protocolVersion = kMatchProtocolVersion;
    PlayerId playerId = 0;
    CommandSequence sequence = 0;
    PlayerCommandPayload payload;
};

enum class CommandRejectionReason : std::uint8_t {
    UnsupportedProtocolVersion,
    UnknownPlayer,
    DuplicateOrOutOfOrderSequence,
    MatchNotRunning,
    UnknownTowerArchetype,
    TowerNotOwnedByPlayer,
    UnknownTower,
    InvalidPlacement,
    InsufficientFunds,
    UpgradeUnavailable,
    InvalidTargetingMode,
    WaveCannotStart,
};

struct CommandAccepted {
    PlayerId playerId = 0;
    CommandSequence sequence = 0;
    SimulationTick appliedAtTick = 0;
};

struct CommandRejected {
    PlayerId playerId = 0;
    CommandSequence sequence = 0;
    CommandRejectionReason reason = CommandRejectionReason::UnknownPlayer;
};

using PlayerCommandResult = std::variant<CommandAccepted, CommandRejected>;

} // namespace multiplayer