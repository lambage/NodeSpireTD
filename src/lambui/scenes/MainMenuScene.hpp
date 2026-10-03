#pragma once

#include "lambui/LuaUiScene.hpp"

#include <string>
#include <vector>

namespace NodeSpireUi {

// Main menu: Play/Options/Exit navigation, built entirely by MainMenu.lua
// using the generic Audio/Scene Lua bindings (hover/click sfx, Scene.GoTo,
// Scene.Quit) -- no scene-specific C++ glue needed.
class MainMenuScene final : public LuaUiScene {
  public:
    explicit MainMenuScene(lambui_backend::VulkanUiRenderer& renderer);

  protected:
    void bindSceneApi(lua_State* lua, AudioEngine& audio) override;
    void onSceneEnter(AudioEngine& audio) override;

  private:
    struct MenuButtonSpec {
        std::string text;
        std::string imageName;
        float yOffset = 0.0f;
        std::string action;
    };

    static MainMenuScene& fromLua(lua_State* lua);
    static int luaReset(lua_State* lua);
    static int luaCreateButton(lua_State* lua);
    void buildUi(AudioEngine& audio);

    std::vector<MenuButtonSpec> buttonSpecs_;
};

} // namespace NodeSpireUi
