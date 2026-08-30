#include "scenes/PlayLevelBootstrap.hpp"

#include "scenes/PlayLevelPickingController.hpp"
#include "utility/WorldRenderer.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <spdlog/spdlog.h>

namespace {

void preloadEnemyArchetypes(EnemyLoadController& enemyLoadController) {
    const std::filesystem::path enemyRoot = "assets/models/enemy";
    const std::filesystem::path defaultScript = enemyRoot / "goblin1.enemy.lua";

    if (enemyLoadController.empty() && !enemyLoadController.loadEnemyArchetype(defaultScript.string())) {
        spdlog::warn("PlayLevelScene: using built-in enemy defaults because no archetype could be loaded.");
        enemyLoadController.registerArchetype(EnemyArchetype{});
    }

    if (!std::filesystem::exists(enemyRoot)) {
        return;
    }

    std::vector<std::filesystem::path> discoveredScripts;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(enemyRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path scriptPath = entry.path();
        if (scriptPath.extension() != ".lua") {
            continue;
        }
        const std::string stem = scriptPath.stem().string();
        if (!stem.ends_with(".enemy")) {
            continue;
        }
        if (scriptPath.lexically_normal() == defaultScript.lexically_normal()) {
            continue;
        }
        discoveredScripts.push_back(scriptPath);
    }

    std::sort(discoveredScripts.begin(), discoveredScripts.end());
    for (const std::filesystem::path& scriptPath : discoveredScripts) {
        if (!enemyLoadController.loadEnemyArchetype(scriptPath.string())) {
            spdlog::warn("PlayLevelScene: failed to preload enemy archetype {}.", scriptPath.string());
        }
    }
}

} // namespace

void PlayLevelBootstrap::resetRuntimeState(
    PlayLevelState& gameplayState, TowerLoadController& towerLoadController, EnemyLoadController& enemyLoadController,
    PlayLevelTowerPlacementController& towerPlacementController, std::vector<playlevel::PlacedTower>& placedTowers,
    std::vector<playlevel::ActiveProjectile>& activeProjectiles, std::uint64_t& nextEnemyRuntimeId,
    std::vector<playlevel::ActiveEnemy>& activeEnemies, PlayLevelWaveController& waveController,
    PlayLevelRouteController& routeController, std::uint64_t& selectedEnemyRuntimeId,
    PlayLevelPickingController& pickingController, const SceneSharedState& state,
    std::filesystem::path& selectedMapAssetPath, std::filesystem::path& selectedLevelScriptPath,
    std::string& selectedWavesScriptPath, WorldAssetSpec& worldAssetSpec) const {
    gameplayState.resetForNewRun();
    towerLoadController.reset();
    enemyLoadController.reset();
    towerPlacementController.reset();
    placedTowers.clear();
    activeProjectiles.clear();
    nextEnemyRuntimeId = 1;
    activeEnemies.clear();
    waveController.clearAll();
    routeController.clear();
    selectedEnemyRuntimeId = 0;
    pickingController.reset();
    selectedMapAssetPath = state.activeLevelAssetPath;
    selectedLevelScriptPath = state.activeLevelScriptPath;
    selectedWavesScriptPath = "assets/scenes/PlayLevelWaves.lua";
    worldAssetSpec = {};
}

void PlayLevelBootstrap::configureLevel(lua_State* luaState, SceneSharedState& state,
                                        std::filesystem::path& selectedMapAssetPath,
                                        std::filesystem::path& selectedLevelScriptPath,
                                        std::string& selectedWavesScriptPath, WorldAssetSpec& worldAssetSpec,
                                        TowerLoadController& towerLoadController,
                                        EnemyLoadController& enemyLoadController,
                                        PlayLevelWaveController& waveController) const {
    if (selectedLevelScriptPath.empty()) {
        selectedLevelScriptPath = "assets/scenes/PlayLevel.level.lua";
    }

    LevelDefinitionLoader loader(luaState);
    PlayLevelDefinition definition;
    if (!loader.load(selectedLevelScriptPath, definition)) {
        spdlog::warn("PlayLevelScene: failed to load level definition {}.", selectedLevelScriptPath.string());
    } else {
        worldAssetSpec = definition.worldAssetSpec;
        if (!definition.wavesScriptPath.empty()) {
            selectedWavesScriptPath = definition.wavesScriptPath;
        }
        if (definition.inheritActiveSelection) {
            selectedMapAssetPath = state.activeLevelAssetPath;
        } else if (!definition.mapAssetPath.empty()) {
            selectedMapAssetPath = definition.mapAssetPath;
        }
        publishLevelUiTextures(luaState, worldAssetSpec);
    }

    preloadEnemyArchetypes(enemyLoadController);

    if (!waveController.hasDefinitions() && !selectedWavesScriptPath.empty() &&
        !waveController.loadWaveDefinitions(
            luaState, selectedWavesScriptPath, enemyLoadController.defaultId(),
            [&enemyLoadController](const std::string& enemyId) -> std::optional<PlayLevelWaveController::EnemyWaveDefaults> {
                const EnemyArchetype* archetype = enemyLoadController.findArchetype(enemyId);
                if (!archetype) {
                    return std::nullopt;
                }
                PlayLevelWaveController::EnemyWaveDefaults defaults;
                defaults.spawnIntervalSeconds = archetype->spawnIntervalSeconds;
                return defaults;
            })) {
        spdlog::warn("PlayLevelScene: using fallback wave definition because {} failed to load.",
                     selectedWavesScriptPath);
    }

    if (!waveController.hasDefinitions()) {
        PlayLevelWaveController::WaveDefinition fallback;
        fallback.spawns.push_back(PlayLevelWaveController::WaveSpawnDefinition{});
        waveController.definitionsMutable().push_back(std::move(fallback));
    }

    towerLoadController.discoverTowerArchetypesInDirectory("assets/models/towers");
    towerLoadController.populateWorldAssets(worldAssetSpec);
    publishLevelUiTextures(luaState, worldAssetSpec);

    std::vector<std::filesystem::path> templateModels;
    templateModels.reserve(enemyLoadController.archetypes().size());
    for (const auto& [id, archetype] : enemyLoadController.archetypes()) {
        (void)id;
        if (archetype.modelPath.empty()) {
            continue;
        }
        const std::filesystem::path modelPath = archetype.modelPath;
        if (std::find(templateModels.begin(), templateModels.end(), modelPath) == templateModels.end()) {
            templateModels.push_back(modelPath);
        }
    }
    if (!templateModels.empty()) {
        worldAssetSpec.animatedTemplateModelPaths = std::move(templateModels);
    }

}

std::unique_ptr<WorldRenderer> PlayLevelBootstrap::beginWorldLoad(lua_State* luaState, SceneSharedState& state,
                                                                  const std::filesystem::path& selectedMapAssetPath,
                                                                  const WorldAssetSpec& worldAssetSpec,
                                                                  std::string& outLoadStatus) const {
    outLoadStatus.clear();

    std::filesystem::path assetPath = selectedMapAssetPath;
    if (std::filesystem::is_directory(assetPath)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetPath)) {
            const auto ext = entry.path().extension().string();
            if (ext == ".glb" || ext == ".gltf") {
                assetPath = entry.path();
                break;
            }
        }
    }

    if (!state.vulkanContext) {
        outLoadStatus = "No Vulkan context available.";
        return nullptr;
    }

    auto worldRenderer = std::make_unique<WorldRenderer>(luaState, *state.vulkanContext);
    worldRenderer->beginLoad(assetPath, worldAssetSpec);
    return worldRenderer;
}
