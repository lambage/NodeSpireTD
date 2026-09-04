#pragma once

#include "rmlui/IScene.hpp"

namespace Rml {
class ElementDocument;
}

namespace NodeSpireUi {

// Splash screen: shows the logo and a status line, then hands off to the
// main menu after a minimum duration (or immediately on any keypress).
//
// Not a 1:1 port of the legacy SplashScene/SplashScreen.lua: the old version
// used a per-frame Lua script to lay out an ImGui window each frame and to
// kick off background music. RmlUi's declarative markup (splash.rml/.rcss)
// replaces the layout half outright, so no Lua is needed for this scene at
// all. Music playback is intentionally dropped for now -- AudioEngine is
// still SFML-based and the SDL3 audio migration hasn't landed yet in this
// app; re-add the music cue once that slice is done.
class SplashScene final : public IScene {
  public:
    void onEnter(Rml::Context& context) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;
    SceneTransition onKeyDown(Rml::Input::KeyIdentifier key) override;

  private:
    Rml::ElementDocument* document_ = nullptr;
    float elapsedSeconds_ = 0.0f;

    static constexpr float kMinimumSplashSeconds = 3.0f;
};

} // namespace NodeSpireUi
