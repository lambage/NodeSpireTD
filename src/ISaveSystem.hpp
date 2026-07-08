#pragma once

#include <optional>
#include <string>
#include <vector>

class ISaveSystem {
public:
    virtual ~ISaveSystem() = default;

    virtual bool saveSlot(const std::string& slotId, const std::vector<std::byte>& data) = 0;
    virtual std::optional<std::vector<std::byte>> loadSlot(const std::string& slotId) const = 0;
};
