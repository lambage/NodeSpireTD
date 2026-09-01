#include "multiplayer/MatchSimulation.hpp"

namespace multiplayer {

void MatchSimulation::reset() {
    playerAccounts_ = {};
    clock_.reset();
    currentTick_ = 0;
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

SimulationTick MatchSimulation::currentTick() const {
    return currentTick_;
}

} // namespace multiplayer