#include "Scenes.hpp"

#include "scenes/LobbyScene.hpp"
#include "scenes/MainMenuScene.hpp"
#include "scenes/OptionsScene.hpp"
#include "scenes/PlayLevelScene.hpp"
#include "scenes/SplashScreen.hpp"

SceneGraph createDefaultScenes() {
    SceneGraph sceneGraph;
    sceneGraph.emplace(SceneId::Splash, std::make_unique<SplashScene>());
    sceneGraph.emplace(SceneId::MainMenu, std::make_unique<MainMenuScene>());
    sceneGraph.emplace(SceneId::Options, std::make_unique<OptionsScene>());
    sceneGraph.emplace(SceneId::Lobby, std::make_unique<LobbyScene>());
    sceneGraph.emplace(SceneId::PlayLevel, std::make_unique<PlayLevelScene>());
    return sceneGraph;
}