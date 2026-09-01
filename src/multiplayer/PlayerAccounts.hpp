#pragma once

#include "multiplayer/MatchProtocol.hpp"

#include <unordered_map>
#include <vector>

namespace multiplayer {

class PlayerAccounts {
  public:
  struct Balance {
    PlayerId playerId = 0;
    float amount = 0.0f;
  };

    bool registerPlayer(PlayerId playerId, float initialBalance);
    bool unregisterPlayer(PlayerId playerId);
    bool hasPlayer(PlayerId playerId) const;
    float balance(PlayerId playerId) const;
    bool debit(PlayerId playerId, float amount);
    bool credit(PlayerId playerId, float amount);
    std::vector<Balance> balances() const;

  private:
    std::unordered_map<PlayerId, float> balances_;
};

} // namespace multiplayer