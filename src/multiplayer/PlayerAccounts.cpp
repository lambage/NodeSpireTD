#include "multiplayer/PlayerAccounts.hpp"

#include <cmath>

namespace multiplayer {

bool PlayerAccounts::registerPlayer(PlayerId playerId, float initialBalance) {
    if (playerId == 0 || !std::isfinite(initialBalance) || initialBalance < 0.0f || balances_.contains(playerId)) {
        return false;
    }
    balances_.emplace(playerId, initialBalance);
    return true;
}

bool PlayerAccounts::unregisterPlayer(PlayerId playerId) {
    return balances_.erase(playerId) != 0;
}

bool PlayerAccounts::hasPlayer(PlayerId playerId) const {
    return balances_.contains(playerId);
}

float PlayerAccounts::balance(PlayerId playerId) const {
    const auto accountIt = balances_.find(playerId);
    return accountIt != balances_.end() ? accountIt->second : 0.0f;
}

bool PlayerAccounts::debit(PlayerId playerId, float amount) {
    if (!std::isfinite(amount) || amount <= 0.0f) {
        return false;
    }
    const auto accountIt = balances_.find(playerId);
    if (accountIt == balances_.end() || accountIt->second < amount) {
        return false;
    }
    accountIt->second -= amount;
    return true;
}

bool PlayerAccounts::credit(PlayerId playerId, float amount) {
    if (!std::isfinite(amount) || amount <= 0.0f) {
        return false;
    }
    const auto accountIt = balances_.find(playerId);
    if (accountIt == balances_.end()) {
        return false;
    }
    accountIt->second += amount;
    return true;
}

} // namespace multiplayer