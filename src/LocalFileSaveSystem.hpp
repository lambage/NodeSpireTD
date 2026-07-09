#pragma once

#include "ISaveSystem.hpp"

#include <filesystem>

class LocalFileSaveSystem : public ISaveSystem {
  public:
    explicit LocalFileSaveSystem(std::filesystem::path rootPath = "saves");

    bool saveSlot(const std::string& slotId, const std::vector<std::byte>& data) override;
    std::optional<std::vector<std::byte>> loadSlot(const std::string& slotId) const override;

  private:
    std::filesystem::path slotPath(const std::string& slotId) const;

    std::filesystem::path rootPath_;
};
