#pragma once

#include "utility/WorldAssetLoader.hpp"

#include <filesystem>
#include <string>

struct lua_State;

struct PlayLevelDefinition {
    std::filesystem::path mapAssetPath;
    std::string wavesScriptPath;
    bool inheritActiveSelection = true;
    WorldAssetSpec worldAssetSpec{};
};

class LevelDefinitionLoader {
  public:
    explicit LevelDefinitionLoader(lua_State* luaState);

    bool load(const std::filesystem::path& scriptPath, PlayLevelDefinition& outDefinition) const;

  private:
    lua_State* L_ = nullptr;
};

void publishLevelUiTextures(lua_State* L, const WorldAssetSpec& spec);
