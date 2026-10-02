#pragma once

#include "lambui/IScene.hpp"

#include <memory>
#include <string>

struct lua_State;

namespace LambUI {
class UIManager;
}

namespace LambUILua {
class LuaUIBindings;
}

namespace lambui_backend {
class VulkanUiRenderer;
}

namespace NodeSpireUi {

// Base for scenes whose UI is built by running a per-scene .lua script
// against LambUI's Lua bindings (the "UI" global: UI.CreateFrame(...),
// widget:SetPoint/SetScript/etc.) instead of a hand-built C++ widget tree.
// Owns a dedicated lua_State for the scene's lifetime: closed on onExit and
// recreated on the next onEnter, so SceneManager::reloadActiveScene() (the
// app's dev /reload hotkey) naturally re-runs the script against a fresh VM
// and a freshly cleared widget tree.
//
// Uses LambUI::UIManager::GetRoot() directly as the scene's widget root
// (cleared via DestroyChildren() on enter/exit) since only one scene is
// ever visible at a time; there is currently no always-on-top UI layered
// across scene transitions. If one is added later, give it its own
// LambUI::UIWidget child and bind scenes via the root-scoped
// LuaUIBindings(lua, manager, root) overload instead of the manager's
// absolute root.
class LuaUiScene : public IScene {
  public:
    LuaUiScene(std::string scriptPath, lambui_backend::VulkanUiRenderer& renderer);
    ~LuaUiScene() override;

    void onEnter(LambUI::UIManager& ui, AudioEngine& audio) override;
    void onExit(LambUI::UIManager& ui) override;
    SceneTransition update(float dt) override;

    // Used by LuaSceneBindings' Scene.GoTo(name) Lua call.
    void requestTransitionFromLua(SceneId id) { requestTransition(id); }

  protected:
    // Hook called once per frame, after the Lua script's optional global
    // "OnUpdate(dt)" function (if defined) has already been invoked.
    virtual void onUpdateScene(float /*dt*/) {}

    // Hook called at the end of onEnter (after the script has run), for
    // subclasses to reset per-enter state (e.g. elapsed timers) on every
    // onEnter, including a /reload re-enter of the same scene.
    virtual void onSceneEnter(AudioEngine& /*audio*/) {}

    void requestTransition(SceneId id) { pendingTransition_ = id; }

  private:
    std::string scriptPath_;
    lambui_backend::VulkanUiRenderer& renderer_;
    lua_State* lua_ = nullptr;
    std::unique_ptr<LambUILua::LuaUIBindings> bindings_;
    SceneTransition pendingTransition_;
};

} // namespace NodeSpireUi
