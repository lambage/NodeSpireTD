#include "scenes/LevelDefinitionLoader.hpp"

#include "lua.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace {

bool isProjectAssetPath(const std::filesystem::path& path) {
    const std::string normalized = path.generic_string();
    return normalized.rfind("assets/", 0) == 0;
}

glm::vec3 luaReadVec3Field(lua_State* L, int tableIndex, const char* fieldName, const glm::vec3& defaultValue) {
    lua_getfield(L, tableIndex, fieldName);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return defaultValue;
    }

    glm::vec3 value = defaultValue;
    lua_getfield(L, -1, "x");
    if (lua_isnumber(L, -1)) {
        value.x = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "y");
    if (lua_isnumber(L, -1)) {
        value.y = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "z");
    if (lua_isnumber(L, -1)) {
        value.z = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);

    lua_pop(L, 1);
    return value;
}

std::filesystem::path resolveAssetPath(const std::filesystem::path& scriptDir, std::filesystem::path rawPath) {
    if (rawPath.is_relative() && !isProjectAssetPath(rawPath)) {
        rawPath = scriptDir / rawPath;
    }
    return rawPath;
}

} // namespace

LevelDefinitionLoader::LevelDefinitionLoader(lua_State* luaState) : L_(luaState) {}

bool LevelDefinitionLoader::load(const std::filesystem::path& scriptPath, PlayLevelDefinition& outDefinition) const {
    if (!L_ || !std::filesystem::exists(scriptPath)) {
        return false;
    }

    if (luaL_loadfile(L_, scriptPath.string().c_str()) != LUA_OK) {
        spdlog::error("PlayLevelScene: failed to load level definition {}: {}", scriptPath.string(),
                      lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
        spdlog::error("PlayLevelScene: level definition execution error {}: {}", scriptPath.string(),
                      lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }

    if (!lua_istable(L_, -1)) {
        spdlog::error("PlayLevelScene: level definition must return a table: {}", scriptPath.string());
        lua_pop(L_, 1);
        return false;
    }

    const std::filesystem::path scriptDir = scriptPath.parent_path();

    lua_getfield(L_, -1, "onLoad");
    if (lua_isfunction(L_, -1)) {
        lua_pushvalue(L_, -2);
        if (lua_pcall(L_, 1, 0, 0) != LUA_OK) {
            spdlog::error("PlayLevelScene: level definition onLoad() execution error {}: {}", scriptPath.string(),
                          lua_tostring(L_, -1));
            lua_pop(L_, 2);
            return false;
        }
    } else {
        lua_pop(L_, 1);
    }

    PlayLevelDefinition definition;

    lua_getfield(L_, -1, "mapAssetPath");
    if (lua_isstring(L_, -1)) {
        std::filesystem::path rawPath = lua_tostring(L_, -1);
        if (!rawPath.empty()) {
            definition.mapAssetPath = resolveAssetPath(scriptDir, std::move(rawPath));
        }
    }
    lua_pop(L_, 1);

    lua_getfield(L_, -1, "wavesScriptPath");
    if (lua_isstring(L_, -1)) {
        std::filesystem::path rawPath = lua_tostring(L_, -1);
        if (!rawPath.empty()) {
            definition.wavesScriptPath = resolveAssetPath(scriptDir, std::move(rawPath)).string();
        }
    }
    lua_pop(L_, 1);

    lua_getfield(L_, -1, "inheritActiveSelection");
    definition.inheritActiveSelection = lua_isboolean(L_, -1) ? lua_toboolean(L_, -1) != 0 : true;
    lua_pop(L_, 1);

    lua_getfield(L_, -1, "worldAssets");
    if (lua_istable(L_, -1)) {
        const int worldAssetsIdx = lua_gettop(L_);

        lua_getfield(L_, worldAssetsIdx, "startModelPath");
        if (lua_isstring(L_, -1)) {
            const std::string path = lua_tostring(L_, -1);
            if (!path.empty()) {
                definition.worldAssetSpec.startModelPath = path;
            }
        }
        lua_pop(L_, 1);

        lua_getfield(L_, worldAssetsIdx, "endModelPath");
        if (lua_isstring(L_, -1)) {
            const std::string path = lua_tostring(L_, -1);
            if (!path.empty()) {
                definition.worldAssetSpec.endModelPath = path;
            }
        }
        lua_pop(L_, 1);

        lua_getfield(L_, worldAssetsIdx, "animatedTemplateModelPaths");
        if (lua_istable(L_, -1)) {
            const int templateTable = lua_gettop(L_);
            const int templateCount = static_cast<int>(lua_rawlen(L_, templateTable));
            for (int i = 1; i <= templateCount; ++i) {
                lua_geti(L_, templateTable, i);
                if (lua_isstring(L_, -1)) {
                    const std::string path = lua_tostring(L_, -1);
                    if (!path.empty()) {
                        definition.worldAssetSpec.animatedTemplateModelPaths.emplace_back(path);
                    }
                }
                lua_pop(L_, 1);
            }
        }
        lua_pop(L_, 1);

        if (definition.worldAssetSpec.animatedTemplateModelPaths.empty()) {
            lua_getfield(L_, worldAssetsIdx, "animatedTemplateModelPath");
            if (lua_isstring(L_, -1)) {
                const std::string path = lua_tostring(L_, -1);
                if (!path.empty()) {
                    definition.worldAssetSpec.animatedTemplateModelPaths.emplace_back(path);
                }
            }
            lua_pop(L_, 1);
        }

        lua_getfield(L_, worldAssetsIdx, "extraWorldModels");
        if (lua_istable(L_, -1)) {
            const int modelsIdx = lua_gettop(L_);
            const int modelCount = static_cast<int>(lua_rawlen(L_, modelsIdx));
            for (int i = 1; i <= modelCount; ++i) {
                lua_geti(L_, modelsIdx, i);
                if (!lua_istable(L_, -1)) {
                    lua_pop(L_, 1);
                    continue;
                }

                WorldModelPlacementSpec placement;

                lua_getfield(L_, -1, "modelPath");
                if (lua_isstring(L_, -1)) {
                    placement.modelPath = lua_tostring(L_, -1);
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "debugGroup");
                if (lua_isstring(L_, -1)) {
                    placement.debugGroup = lua_tostring(L_, -1);
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "debugLabel");
                if (lua_isstring(L_, -1)) {
                    placement.debugLabel = lua_tostring(L_, -1);
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "anchor");
                if (lua_isstring(L_, -1)) {
                    const std::string anchor = lua_tostring(L_, -1);
                    if (anchor == "Start") {
                        placement.anchor = WorldMarkerAnchor::Start;
                    } else if (anchor == "End") {
                        placement.anchor = WorldMarkerAnchor::End;
                    }
                }
                lua_pop(L_, 1);

                lua_getfield(L_, -1, "facePath");
                if (lua_isboolean(L_, -1)) {
                    placement.facePath = lua_toboolean(L_, -1) != 0;
                }
                lua_pop(L_, 1);

                placement.positionOffset = luaReadVec3Field(L_, -1, "positionOffset", placement.positionOffset);
                placement.eulerDegrees = luaReadVec3Field(L_, -1, "eulerDegrees", placement.eulerDegrees);
                placement.scale = luaReadVec3Field(L_, -1, "scale", placement.scale);

                if (!placement.modelPath.empty()) {
                    if (placement.debugLabel.empty()) {
                        placement.debugLabel = placement.modelPath.filename().string();
                    }
                    definition.worldAssetSpec.extraWorldModels.push_back(std::move(placement));
                }
                lua_pop(L_, 1);
            }
        }
        lua_pop(L_, 1);

        lua_getfield(L_, worldAssetsIdx, "uiTextures");
        if (lua_istable(L_, -1)) {
            const int texturesIdx = lua_gettop(L_);
            const int textureCount = static_cast<int>(lua_rawlen(L_, texturesIdx));
            for (int i = 1; i <= textureCount; ++i) {
                lua_geti(L_, texturesIdx, i);

                WorldUiTextureSpec texSpec;
                if (lua_isstring(L_, -1)) {
                    texSpec.texturePath = lua_tostring(L_, -1);
                } else if (lua_istable(L_, -1)) {
                    lua_getfield(L_, -1, "id");
                    if (lua_isstring(L_, -1)) {
                        texSpec.id = lua_tostring(L_, -1);
                    }
                    lua_pop(L_, 1);

                    lua_getfield(L_, -1, "path");
                    if (lua_isstring(L_, -1)) {
                        texSpec.texturePath = lua_tostring(L_, -1);
                    }
                    lua_pop(L_, 1);
                }

                if (!texSpec.texturePath.empty()) {
                    if (texSpec.id.empty()) {
                        texSpec.id = texSpec.texturePath.stem().string();
                    }
                    definition.worldAssetSpec.uiTextures.push_back(std::move(texSpec));
                }

                lua_pop(L_, 1);
            }
        }
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);

    lua_pop(L_, 1);
    outDefinition = std::move(definition);
    return true;
}

void publishLevelUiTextures(lua_State* L, const WorldAssetSpec& spec) {
    lua_newtable(L);
    const int rootTable = lua_gettop(L);
    for (std::size_t i = 0; i < spec.uiTextures.size(); ++i) {
        const auto& entry = spec.uiTextures[i];
        lua_newtable(L);

        lua_pushstring(L, entry.id.c_str());
        lua_setfield(L, -2, "id");

        const std::string path = entry.texturePath.string();
        lua_pushstring(L, path.c_str());
        lua_setfield(L, -2, "path");

        lua_seti(L, rootTable, static_cast<lua_Integer>(i + 1));
    }
    lua_setglobal(L, "LevelUiTextures");
}
