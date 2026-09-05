#pragma once

#include "rmlui/IScene.hpp"

#include <RmlUi/Core/EventListener.h>

#include <optional>

class AudioEngine;

namespace Rml {
class ElementDocument;
}

namespace NodeSpireUi {

// Placeholder lobby screen reached from MainMenu's Play button. Just a Back
// button for now; the real multiplayer lobby (host/join/level select) is a
// follow-up session -- see the legacy LobbyScene in src/scenes/ for the
// feature set to port.
class LobbyScene final : public IScene, public Rml::EventListener {
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
