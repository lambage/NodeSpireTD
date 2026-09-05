#pragma once

#include "rmlui/IScene.hpp"

namespace Rml {
class ElementDocument;
}

namespace NodeSpireUi {

// Splash screen: shows the logo and a status line, then hands off to the
// main menu after a minimum duration (or immediately on any keypress).
// RmlUi's declarative markup owns the layout, while this class starts the
// looped background music through AudioEngine.
class SplashScene final : public IScene {
  public:
    void onEnter(Rml::Context& context, AudioEngine& audio) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;
    SceneTransition onKeyDown(Rml::Input::KeyIdentifier key) override;

  private:
    Rml::ElementDocument* document_ = nullptr;
    float elapsedSeconds_ = 0.0f;

    static constexpr float kMinimumSplashSeconds = 3.0f;
};

} // namespace NodeSpireUi
