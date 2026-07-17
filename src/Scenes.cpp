#include "Scenes.hpp"

#include "scenes/LevelSelectionScene.hpp"
#include "scenes/MainMenuScene.hpp"
#include "scenes/OptionsScene.hpp"
#include "scenes/PlayLevelScene.hpp"
#include "scenes/SplashScreen.hpp"

SceneGraph createDefaultScenes(lua_State* L) {
    SceneGraph sceneGraph;
    sceneGraph.emplace(SceneId::Splash, std::make_unique<SplashScene>(L));
    sceneGraph.emplace(SceneId::MainMenu, std::make_unique<MainMenuScene>(L));
    sceneGraph.emplace(SceneId::Options, std::make_unique<OptionsScene>(L));
    sceneGraph.emplace(SceneId::LevelSelection, std::make_unique<LevelSelectionScene>(L));
    sceneGraph.emplace(SceneId::PlayLevel, std::make_unique<PlayLevelScene>(L));
    return sceneGraph;
}