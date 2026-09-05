#pragma once

#include "rmlui/IScene.hpp"

#include <RmlUi/Core/EventListener.h>

#include <optional>

class AudioEngine;

namespace Rml {
class ElementDocument;
}

namespace NodeSpireUi {

// Main menu: Play/Options/Exit navigation. Buttons are wired up with plain
// C++ event listeners (this class doubles as the Rml::EventListener) rather
// than Lua, since this scene has no gameplay state to script yet; see
// rmlui-lua-scene-integration skill notes for when Lua gets introduced.
class MainMenuScene final : public IScene, public Rml::EventListener {
  public:
    void onEnter(Rml::Context& context, AudioEngine& audio) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;

    void ProcessEvent(Rml::Event& event) override;

  private:
    Rml::ElementDocument* document_ = nullptr;
    AudioEngine* audio_ = nullptr;
    SceneTransition pendingTransition_;
};

} // namespace NodeSpireUi

