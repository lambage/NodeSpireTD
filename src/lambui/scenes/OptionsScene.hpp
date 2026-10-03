#pragma once

#include "lambui/LuaUiScene.hpp"

struct AppSettings;
struct SDL_Window;
class VulkanContext;

namespace NodeSpireUi {

class OptionsScene final : public LuaUiScene {
  public:
    OptionsScene(lambui_backend::VulkanUiRenderer& renderer, AppSettings& settings,
                 SDL_Window* window, VulkanContext& vulkanContext);
    SceneTransition onKeyDown(uint32_t scanCode) override;

  protected:
    void bindSceneApi(lua_State* lua, AudioEngine& audio) override;

  private:
    static OptionsScene& fromLua(lua_State* lua);
    bool apply(const AppSettings& settings, std::string& error);

    AppSettings& settings_;
    SDL_Window* window_;
    VulkanContext& vulkanContext_;
    AudioEngine* audio_ = nullptr;
};

} // namespace NodeSpireUi

