#pragma once

#include "multiplayer/MatchProtocol.hpp"

#include <unordered_map>

namespace multiplayer {

class PlayerAccounts {
  public:
    bool registerPlayer(PlayerId playerId, float initialBalance);
    bool unregisterPlayer(PlayerId playerId);
    bool hasPlayer(PlayerId playerId) const;
    float balance(PlayerId playerId) const;
    bool debit(PlayerId playerId, float amount);
    bool credit(PlayerId playerId, float amount);

  private:
    std::unordered_map<PlayerId, float> balances_;
};

} // namespace multiplayer