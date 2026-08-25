#pragma once

#include <cstdint>
#include <string_view>

namespace playlevel {

enum class DamageType : std::uint8_t {
    Physical = 0,
    Fire,
    Poison,
    Arcane,
    Electric,
    Holy,
    Necrotic,
};

struct DamageTypeHash {
    std::size_t operator()(DamageType type) const noexcept {
        return static_cast<std::size_t>(type);
    }
};

inline const char* damageTypeToString(DamageType type) {
    switch (type) {
    case DamageType::Physical:
        return "physical";
    case DamageType::Fire:
        return "fire";
    case DamageType::Poison:
        return "poison";
    case DamageType::Arcane:
        return "arcane";
    case DamageType::Electric:
        return "electric";
    case DamageType::Holy:
        return "holy";
    case DamageType::Necrotic:
        return "necrotic";
    }
    return "physical";
}

inline bool tryParseDamageType(std::string_view value, DamageType& outType) {
    if (value == "physical") {
        outType = DamageType::Physical;
        return true;
    }
    if (value == "fire") {
        outType = DamageType::Fire;
        return true;
    }
    if (value == "poison") {
        outType = DamageType::Poison;
        return true;
    }
    if (value == "arcane") {
        outType = DamageType::Arcane;
        return true;
    }
    if (value == "electric") {
        outType = DamageType::Electric;
        return true;
    }
    if (value == "holy") {
        outType = DamageType::Holy;
        return true;
    }
    if (value == "necrotic") {
        outType = DamageType::Necrotic;
        return true;
    }
    return false;
}

} // namespace playlevel
