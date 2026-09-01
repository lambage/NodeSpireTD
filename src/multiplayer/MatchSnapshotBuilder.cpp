#include "multiplayer/MatchSnapshotBuilder.hpp"

#include "nodespire/multiplayer/v1/match.pb.h"

#include <algorithm>
#include <vector>

namespace multiplayer {
namespace {

nodespire::multiplayer::v1::MatchStatus toWireMatchStatus(MatchStatus status) {
    switch (status) {
    case MatchStatus::WaitingToStart: return nodespire::multiplayer::v1::MATCH_STATUS_WAITING_TO_START;
    case MatchStatus::Running: return nodespire::multiplayer::v1::MATCH_STATUS_RUNNING;
    case MatchStatus::Paused: return nodespire::multiplayer::v1::MATCH_STATUS_PAUSED;
    case MatchStatus::Victory: return nodespire::multiplayer::v1::MATCH_STATUS_VICTORY;
    case MatchStatus::Defeat: return nodespire::multiplayer::v1::MATCH_STATUS_DEFEAT;
    }
    return nodespire::multiplayer::v1::MATCH_STATUS_UNSPECIFIED;
}

nodespire::multiplayer::v1::TowerTargetingMode toWireTargetingMode(playlevel::TowerTargetingMode mode) {
    using namespace nodespire::multiplayer::v1;
    switch (mode) {
    case playlevel::TowerTargetingMode::First: return TOWER_TARGETING_MODE_FIRST;
    case playlevel::TowerTargetingMode::Last: return TOWER_TARGETING_MODE_LAST;
    case playlevel::TowerTargetingMode::Nearest: return TOWER_TARGETING_MODE_NEAREST;
    case playlevel::TowerTargetingMode::Random: return TOWER_TARGETING_MODE_RANDOM;
    case playlevel::TowerTargetingMode::HighestHp: return TOWER_TARGETING_MODE_HIGHEST_HP;
    case playlevel::TowerTargetingMode::LowestHp: return TOWER_TARGETING_MODE_LOWEST_HP;
    }
    return TOWER_TARGETING_MODE_UNSPECIFIED;
}

} // namespace

std::optional<std::string> MatchSnapshotBuilder::serialize(const MatchSimulation& simulation) {
    nodespire::multiplayer::v1::MatchSnapshot snapshot;
    const PlayLevelState& state = simulation.gameplayState();
    snapshot.set_simulation_tick(simulation.currentTick());
    snapshot.set_match_status(toWireMatchStatus(state.matchStatus));
    snapshot.set_base_health(state.baseHealth);
    snapshot.set_current_wave(static_cast<std::uint32_t>(std::max(0, state.currentWave)));
    snapshot.set_wave_in_progress(state.waveInProgress);

    for (const PlayerAccounts::Balance& balance : simulation.playerBalances()) {
        auto* player = snapshot.add_players();
        player->set_player_id(balance.playerId);
        player->set_money(balance.amount);
    }

    std::vector<const playlevel::PlacedTower*> towers;
    for (const auto& tower : simulation.placedTowers()) towers.push_back(&tower);
    std::sort(towers.begin(), towers.end(), [](const auto* left, const auto* right) { return left->runtimeId < right->runtimeId; });
    for (const auto* tower : towers) {
        auto* wireTower = snapshot.add_towers();
        wireTower->set_runtime_id(tower->runtimeId);
        wireTower->set_owner_player_id(tower->ownerPlayerId);
        wireTower->set_tower_archetype_id(tower->towerId);
        wireTower->mutable_position()->set_x(tower->position.x);
        wireTower->mutable_position()->set_y(tower->position.y);
        wireTower->mutable_position()->set_z(tower->position.z);
        wireTower->set_targeting_mode(toWireTargetingMode(tower->targetingMode));
        for (const std::string& upgrade : tower->unlockedUpgradeNodeIds) wireTower->add_unlocked_upgrade_node_ids(upgrade);
    }

    std::vector<const playlevel::ActiveEnemy*> enemies;
    for (const auto& enemy : simulation.activeEnemies()) enemies.push_back(&enemy);
    std::sort(enemies.begin(), enemies.end(), [](const auto* left, const auto* right) { return left->runtimeId < right->runtimeId; });
    for (const auto* enemy : enemies) {
        auto* wireEnemy = snapshot.add_enemies();
        wireEnemy->set_runtime_id(enemy->runtimeId);
        wireEnemy->set_enemy_archetype_id(enemy->enemyId);
        wireEnemy->set_distance_along_path(enemy->distanceAlongPath);
        wireEnemy->set_health(enemy->health);
        wireEnemy->set_shield(enemy->shield);
        wireEnemy->set_lifecycle_state(static_cast<std::uint32_t>(enemy->lifecycleState));
    }

    std::vector<const playlevel::ActiveProjectile*> projectiles;
    for (const auto& projectile : simulation.activeProjectiles()) projectiles.push_back(&projectile);
    std::sort(projectiles.begin(), projectiles.end(), [](const auto* left, const auto* right) {
        return left->runtimeId < right->runtimeId;
    });
    for (const auto* projectile : projectiles) {
        auto* wireProjectile = snapshot.add_projectiles();
        wireProjectile->set_runtime_id(projectile->runtimeId);
        wireProjectile->set_tower_archetype_id(projectile->towerId);
        wireProjectile->mutable_position()->set_x(projectile->position.x);
        wireProjectile->mutable_position()->set_y(projectile->position.y);
        wireProjectile->mutable_position()->set_z(projectile->position.z);
        wireProjectile->set_target_enemy_runtime_id(projectile->targetEnemyRuntimeId);
    }

    std::string bytes;
    return snapshot.SerializeToString(&bytes) ? std::optional{std::move(bytes)} : std::nullopt;
}

} // namespace multiplayer