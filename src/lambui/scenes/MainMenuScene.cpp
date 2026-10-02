#include "lambui/scenes/MainMenuScene.hpp"

namespace NodeSpireUi {

MainMenuScene::MainMenuScene(lambui_backend::VulkanUiRenderer& renderer)
	: LuaUiScene("assets/scenes/MainMenu.lua", renderer) {}

} // namespace NodeSpireUi

