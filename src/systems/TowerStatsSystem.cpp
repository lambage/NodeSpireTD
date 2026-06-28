#include "systems/TowerStatsSystem.h"

#include "ecs/Components.h"
#include "talent/StatModifier.h"

namespace NST {

TowerStatsSystem::TowerStatsSystem(entt::registry& registry) : m_registry(registry) {}

// ---------------------------------------------------------------
void TowerStatsSystem::onTalentUpgraded(const TalentUpgradedEvent& event) {
    recalculateStats(event.towerId, event.activeModifiers);
}

void TowerStatsSystem::onTalentRespec(const TalentRespecEvent& event) {
    recalculateStats(event.towerId, {}); // no active modifiers → base stats
}

// ---------------------------------------------------------------
void TowerStatsSystem::recalculateStats(uint32_t towerId, const std::vector<StatModifier>& modifiers) {
    auto view = m_registry.view<TowerStatsComponent, TalentTreeComponent>();

    for (auto entity : view) {
        auto& talent = view.get<TalentTreeComponent>(entity);
        if (talent.towerId != towerId) {
            continue;
        }

        auto& stats = view.get<TowerStatsComponent>(entity);

        // Reset to base values.
        stats = TowerStatsComponent{};

        // --- Pass 1: Additive ---
        for (const auto& mod : modifiers) {
            if (mod.op != ModifierOp::Additive) {
                continue;
            }
            switch (mod.stat) {
            case StatType::AttackSpeed:
                stats.attackSpeed += mod.value;
                break;
            case StatType::Damage:
                stats.damage += mod.value;
                break;
            case StatType::Range:
                stats.range += mod.value;
                break;
            case StatType::AoE:
                stats.aoe += mod.value;
                break;
            default:
                break;
            }
        }

        // --- Pass 2: Multiplicative ---
        for (const auto& mod : modifiers) {
            if (mod.op != ModifierOp::Multiplicative) {
                continue;
            }
            switch (mod.stat) {
            case StatType::AttackSpeed:
                stats.attackSpeed *= (1.0f + mod.value);
                break;
            case StatType::Damage:
                stats.damage *= (1.0f + mod.value);
                break;
            case StatType::Range:
                stats.range *= (1.0f + mod.value);
                break;
            case StatType::AoE:
                stats.aoe *= (1.0f + mod.value);
                break;
            default:
                break;
            }
        }

        // --- Pass 3: Override (last-writer wins) ---
        for (const auto& mod : modifiers) {
            if (mod.op != ModifierOp::Override) {
                continue;
            }
            switch (mod.stat) {
            case StatType::AttackSpeed:
                stats.attackSpeed = mod.value;
                break;
            case StatType::Damage:
                stats.damage = mod.value;
                break;
            case StatType::Range:
                stats.range = mod.value;
                break;
            case StatType::AoE:
                stats.aoe = mod.value;
                break;
            default:
                break;
            }
        }
    }
}

} // namespace NST
