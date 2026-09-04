#include "multiplayer/PlayerProfileStore.hpp"

#include "multiplayer/PartyProtocol.hpp"

#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <spdlog/spdlog.h>

namespace multiplayer {
namespace {

std::string generatePlayerUuid() {
    std::random_device randomDevice;
    std::mt19937_64 generator(randomDevice());
    std::uniform_int_distribution<std::uint64_t> distribution;

    std::uint64_t hi = distribution(generator);
    std::uint64_t lo = distribution(generator);
    // Set the RFC 4122 version (4, random) and variant (10xx) bits.
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(8) << static_cast<std::uint32_t>(hi >> 32) << "-"
           << std::setw(4) << static_cast<std::uint16_t>(hi >> 16) << "-" << std::setw(4)
           << static_cast<std::uint16_t>(hi) << "-" << std::setw(4) << static_cast<std::uint16_t>(lo >> 48) << "-"
           << std::setw(12) << (lo & 0xFFFFFFFFFFFFULL);
    return stream.str();
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

} // namespace

PlayerProfileStore::PlayerProfileStore(std::filesystem::path profileFilePath)
    : profileFilePath_(std::move(profileFilePath)) {
    load();
}

void PlayerProfileStore::load() {
    profile_ = PlayerProfile{};

    if (std::filesystem::exists(profileFilePath_)) {
        std::ifstream input(profileFilePath_);
        if (input.is_open()) {
            try {
                nlohmann::json json;
                input >> json;
                profile_.playerUuid = json.value("playerUuid", std::string());
                profile_.displayName = json.value("displayName", profile_.displayName);
            } catch (const std::exception& ex) {
                spdlog::warn("PlayerProfileStore: failed to parse {} ({}). Regenerating.", profileFilePath_.string(),
                             ex.what());
            }
        }
    }

    // A missing/blank UUID means this is either the first run or a corrupted file -- either way,
    // mint a fresh stable identity rather than leaving it empty (an empty UUID is treated by the
    // host as "always a new player", which defeats reconnection).
    if (profile_.playerUuid.empty()) {
        profile_.playerUuid = generatePlayerUuid();
    }
    const std::string trimmedName = trim(profile_.displayName);
    profile_.displayName = trimmedName.empty() ? "Player" : trimmedName.substr(0, kMaxPartyDisplayNameLength);

    save();
}

bool PlayerProfileStore::save() const {
    try {
        std::filesystem::create_directories(profileFilePath_.parent_path());

        std::ofstream output(profileFilePath_, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        const nlohmann::json json{{"playerUuid", profile_.playerUuid}, {"displayName", profile_.displayName}};
        output << json.dump(4) << '\n';
        return true;
    } catch (const std::exception& ex) {
        spdlog::warn("PlayerProfileStore: failed to save {} ({}).", profileFilePath_.string(), ex.what());
        return false;
    }
}

bool PlayerProfileStore::setDisplayName(const std::string& displayName) {
    const std::string trimmedName = trim(displayName);
    if (trimmedName.empty() || trimmedName.size() > kMaxPartyDisplayNameLength) {
        return false;
    }
    profile_.displayName = trimmedName;
    return save();
}

} // namespace multiplayer
