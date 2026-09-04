#pragma once

#include "rmlui/IScene.hpp"

namespace Rml {
class ElementDocument;
}

namespace NodeSpireUi {

// Placeholder main menu, just enough to prove SplashScene -> MainMenu
// handoff works end to end. The real main menu (buttons, Lua bindings, a
// GameButton-equivalent) is a follow-up session; see
// rmlui-lua-scene-integration skill notes for that design.
class MainMenuScene final : public IScene {
  public:
    void onEnter(Rml::Context& context) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;

  private:
    Rml::ElementDocument* document_ = nullptr;
};

} // namespace NodeSpireUi
