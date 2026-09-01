#pragma once

#include "scenes/EnemyLoadController.hpp"
#include "scenes/LevelDefinitionLoader.hpp"
#include "scenes/PlayLevelCombatController.hpp"
#include "scenes/PlayLevelRouteController.hpp"
#include "scenes/PlayLevelState.hpp"
#include "scenes/PlayLevelTowerPlacementController.hpp"
#include "scenes/PlayLevelWaveController.hpp"
#include "scenes/SceneSharedState.hpp"
#include "scenes/TowerLoadController.hpp"
#include "utility/WorldAssetLoader.hpp"

#include <filesystem>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class WorldRenderer;
class PlayLevelPickingController;

class PlayLevelBootstrap {
  public:
    void resetRuntimeState(PlayLevelState& gameplayState, TowerLoadController& towerLoadController,
                           EnemyLoadController& enemyLoadController,
                           PlayLevelTowerPlacementController& towerPlacementController,
                           std::vector<playlevel::PlacedTower>& placedTowers,
                           std::vector<playlevel::ActiveProjectile>& activeProjectiles,
                           std::uint64_t& nextTowerRuntimeId, std::uint64_t& nextEnemyRuntimeId,
                           std::vector<playlevel::ActiveEnemy>& activeEnemies,
                           PlayLevelWaveController& waveController, PlayLevelRouteController& routeController,
                           std::uint64_t& selectedEnemyRuntimeId, PlayLevelPickingController& pickingController,
                           const SceneSharedState& state, std::filesystem::path& selectedMapAssetPath,
                           std::filesystem::path& selectedLevelScriptPath, std::string& selectedWavesScriptPath,
                           WorldAssetSpec& worldAssetSpec) const;

    void configureLevel(lua_State* luaState, SceneSharedState& state, std::filesystem::path& selectedMapAssetPath,
                        std::filesystem::path& selectedLevelScriptPath, std::string& selectedWavesScriptPath,
                        WorldAssetSpec& worldAssetSpec, TowerLoadController& towerLoadController,
                        EnemyLoadController& enemyLoadController, PlayLevelWaveController& waveController) const;

    std::unique_ptr<WorldRenderer> beginWorldLoad(lua_State* luaState, SceneSharedState& state,
                                                  const std::filesystem::path& selectedMapAssetPath,
                                                  const WorldAssetSpec& worldAssetSpec, std::string& outLoadStatus) const;
};
