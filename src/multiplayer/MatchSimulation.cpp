#include "multiplayer/MatchSimulation.hpp"

namespace multiplayer {

void MatchSimulation::reset() {
    playerAccounts_ = {};
    clock_.reset();
    currentTick_ = 0;
    gameplayState_.resetForNewRun();
    waveController_.clearAll();
    placedTowers_.clear();
    activeProjectiles_.clear();
    activeEnemies_.clear();
    nextTowerRuntimeId_ = 1;
    nextEnemyRuntimeId_ = 1;
    nextProjectileRuntimeId_ = 1;
}

bool MatchSimulation::registerPlayer(PlayerId playerId, float initialBalance) {
    return playerAccounts_.registerPlayer(playerId, initialBalance);
}

bool MatchSimulation::unregisterPlayer(PlayerId playerId) {
    return playerAccounts_.unregisterPlayer(playerId);
}

bool MatchSimulation::hasPlayer(PlayerId playerId) const {
    return playerAccounts_.hasPlayer(playerId);
}

float MatchSimulation::playerBalance(PlayerId playerId) const {
    return playerAccounts_.balance(playerId);
}

bool MatchSimulation::debitPlayer(PlayerId playerId, float amount) {
    return playerAccounts_.debit(playerId, amount);
}

bool MatchSimulation::creditPlayer(PlayerId playerId, float amount) {
    return playerAccounts_.credit(playerId, amount);
}

std::vector<PlayerAccounts::Balance> MatchSimulation::playerBalances() const {
    return playerAccounts_.balances();
}

SimulationTick MatchSimulation::currentTick() const {
    return currentTick_;
}

PlayLevelState& MatchSimulation::gameplayState() {
    return gameplayState_;
}

const PlayLevelState& MatchSimulation::gameplayState() const {
    return gameplayState_;
}

PlayLevelWaveController& MatchSimulation::waveController() {
    return waveController_;
}

PlayLevelCombatController& MatchSimulation::combatController() {
    return combatController_;
}

std::vector<playlevel::PlacedTower>& MatchSimulation::placedTowers() {
    return placedTowers_;
}

const std::vector<playlevel::PlacedTower>& MatchSimulation::placedTowers() const {
    return placedTowers_;
}

std::vector<playlevel::ActiveProjectile>& MatchSimulation::activeProjectiles() {
    return activeProjectiles_;
}

const std::vector<playlevel::ActiveProjectile>& MatchSimulation::activeProjectiles() const {
    return activeProjectiles_;
}

std::vector<playlevel::ActiveEnemy>& MatchSimulation::activeEnemies() {
    return activeEnemies_;
}

const std::vector<playlevel::ActiveEnemy>& MatchSimulation::activeEnemies() const {
    return activeEnemies_;
}

TowerRuntimeId& MatchSimulation::nextTowerRuntimeId() {
    return nextTowerRuntimeId_;
}

std::uint64_t& MatchSimulation::nextEnemyRuntimeId() {
    return nextEnemyRuntimeId_;
}

std::uint64_t& MatchSimulation::nextProjectileRuntimeId() {
    return nextProjectileRuntimeId_;
}

} // namespace multiplayer