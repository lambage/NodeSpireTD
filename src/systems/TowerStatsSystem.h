#pragma once

#include <entt/entt.hpp>

#include "ecs/Events.h"

namespace NST {

// ---------------------------------------------------------------
// TowerStatsSystem
//
// Observer-based system that recalculates TowerStatsComponent
// values in response to talent events.  Stats are recalculated
// exactly once per event and then cached in the component; they are
// never evaluated per-frame.
//
// Modifier application order (all per stat type):
//   1. Additive       : stat += value  (stacks linearly)
//   2. Multiplicative : stat *= (1 + value)  (stacks multiplicatively)
//   3. Override       : stat  = value  (last-writer wins)
// ---------------------------------------------------------------
class TowerStatsSystem {
public:
    explicit TowerStatsSystem(entt::registry& registry);

    // Connect to an EnTT dispatcher:
    //   dispatcher.sink<TalentUpgradedEvent>().connect<&TowerStatsSystem::onTalentUpgraded>(this);
    //   dispatcher.sink<TalentRespecEvent>().connect<&TowerStatsSystem::onTalentRespec>(this);
    void onTalentUpgraded(const TalentUpgradedEvent& event);
    void onTalentRespec(const TalentRespecEvent& event);

private:
    void recalculateStats(uint32_t towerId,
                          const std::vector<StatModifier>& modifiers);

    entt::registry& m_registry;
};

} // namespace NST
