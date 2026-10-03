#pragma once

#include "lambui/LuaUiScene.hpp"

namespace NodeSpireUi {

// Main menu: Play/Options/Exit navigation, built entirely by MainMenu.lua
// using the generic Audio/Scene Lua bindings (hover/click sfx, Scene.GoTo,
// Scene.Quit) -- no scene-specific C++ glue needed.
class MainMenuScene final : public LuaUiScene {
  public:
    explicit MainMenuScene(lambui_backend::VulkanUiRenderer& renderer);
};

} // namespace NodeSpireUi

