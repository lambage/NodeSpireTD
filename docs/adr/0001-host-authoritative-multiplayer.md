# 0001: Host-Authoritative Cooperative Multiplayer

## Status

Accepted on 2026-08-31.

## Context

NodeSpireTD is becoming a cooperative multiplayer tower-defense game. Each player has a local camera view and individually owns money and placed towers. The initial release target is a player-hosted LAN game: a host starts a match and clients join using an IPv4 address. A future Steam release must be able to replace the LAN transport without changing match rules or simulation ownership.

Lua supplies game content and UI, so client Lua and client assets must be treated as untrusted. A client must not be able to gain money, alter tower damage, down enemies, or advance waves by changing local scripts or process memory.

## Decision

The host is authoritative for every simulation-affecting state transition:

- player balances, tower ownership, tower placement, upgrades, and targeting modes;
- wave start and spawn progression;
- enemy state, combat, projectiles, rewards, base health, and match outcome.

Each client owns only presentation and input: camera, rendering, UI, and audiovisual feedback. A client sends typed intent requests. The host independently resolves archetypes and validates ownership, balance, prerequisites, placement, rate limits, and match state. The host then broadcasts a command result and authoritative simulation state.

Single player remains a local host with one local client through the same command-validation boundary.

The simulation will advance in fixed ticks. The host assigns stable runtime identifiers for players, towers, enemies, and projectiles. Network messages use Protobuf schemas with explicit, versioned fields rather than raw C++ structures.

The transport is hidden behind a narrow interface. Stage 1 uses an in-process loopback implementation. Stage 2 uses a reliable LAN IPv4 implementation. A Steam networking implementation can later satisfy the same interface.

## Player Command Contract

Initial client-to-host requests are:

- `PlaceTower { requestId, playerId, towerArchetypeId, position }`
- `UpgradeTower { requestId, playerId, towerRuntimeId, upgradeNodeId }`
- `SetTowerTargeting { requestId, playerId, towerRuntimeId, targetingMode }`
- `StartWave { requestId, playerId }`

The host returns `CommandAccepted` or `CommandRejected` for every request. Rejections carry a user-facing reason but never change simulation state.

`SpendMoney` and `DamageBase` are internal simulation operations, not remotely callable player commands. Client messages never contain cost, damage, health, rewards, tower statistics, or authoritative enemy state.

## Replication Contract

The host sends:

- a full snapshot for initial connection and reconnection;
- ordered accepted-command events for immediate UI reconciliation;
- periodic snapshots or deltas for player state, towers, enemies, projectiles, and match state.

Clients interpolate replicated visual state, but do not authoritatively award rewards or resolve combat. The host sends a content manifest hash during the connection handshake and rejects clients whose gameplay content does not match.

## Consequences

- Modified client Lua can change a player's display but not shared gameplay outcomes.
- The player-host can still modify their own process and cheat. Preventing that requires a dedicated server operated independently of players.
- `PlayLevelScene` must be separated into authoritative simulation and client presentation/input responsibilities before sockets are introduced.
- The existing local gameplay command queue is an initial migration seam, but it must evolve into ownership-aware player intents.

## Migration Plan

1. Extract a fixed-tick `MatchSimulation` from `PlayLevelScene`; add player balances, tower ownership, stable IDs, and host command validation with loopback tests.
2. Refactor local tower placement, upgrades, targeting, and wave start to issue player intent commands. Preserve single-player through local host authority.
3. Define Protobuf packet serialization and a loopback host/client harness. Add snapshot application and visual interpolation.
4. Add reliable LAN IPv4 transport, host/join lobby controls, connection lifecycle, content-manifest handshake, and reconnection snapshots.
5. Implement a Steam transport adapter behind the established transport interface.