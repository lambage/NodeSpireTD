#include "LocalFileSaveSystem.hpp"

#include <fstream>

LocalFileSaveSystem::LocalFileSaveSystem(std::filesystem::path rootPath)
    : rootPath_(std::move(rootPath)) {}

bool LocalFileSaveSystem::saveSlot(const std::string& slotId, const std::vector<std::byte>& data) {
    if (slotId.empty()) {
        return false;
    }

    try {
        std::filesystem::create_directories(rootPath_);

        std::ofstream output(slotPath(slotId), std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return output.good();
    } catch (...) {
        return false;
    }
}

std::optional<std::vector<std::byte>> LocalFileSaveSystem::loadSlot(const std::string& slotId) const {
    if (slotId.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path path = slotPath(slotId);
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        return std::nullopt;
    }

    const std::streamsize fileSize = input.tellg();
    if (fileSize < 0) {
        return std::nullopt;
    }

    input.seekg(0, std::ios::beg);

    std::vector<std::byte> data(static_cast<size_t>(fileSize));
    if (!input.read(reinterpret_cast<char*>(data.data()), fileSize)) {
        return std::nullopt;
    }

    return data;
}

std::filesystem::path LocalFileSaveSystem::slotPath(const std::string& slotId) const {
    return rootPath_ / (slotId + ".sav");
}
