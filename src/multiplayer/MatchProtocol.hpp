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

struct SellTowerCommand {
    TowerRuntimeId towerRuntimeId = 0;
};

using PlayerCommandPayload =
    std::variant<PlaceTowerCommand, UpgradeTowerCommand, SetTowerTargetingCommand, StartWaveCommand, SellTowerCommand>;

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
    InvalidPayload,
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

// Sent once by a connecting client before any PlayerCommandRequest. The host rejects the
// connection outright on mismatch rather than allowing it to affect simulation state.
struct ContentManifest {
    std::string gameplayContentSha256;
};

struct JoinMatchRequest {
    std::uint16_t protocolVersion = kMatchProtocolVersion;
    std::string playerDisplayName;
    ContentManifest contentManifest;
};

enum class JoinRejectionReason : std::uint8_t {
    Unspecified,
    ProtocolVersionUnsupported,
    ContentManifestMismatch,
    MatchUnavailable,
    MatchFull,
};

struct JoinMatchAccepted {
    PlayerId playerId = 0;
    SimulationTick currentTick = 0;
};

struct JoinMatchRejected {
    JoinRejectionReason reason = JoinRejectionReason::Unspecified;
};

using JoinMatchResult = std::variant<JoinMatchAccepted, JoinMatchRejected>;

} // namespace multiplayer