#pragma once

#include "rmlui/IScene.hpp"

#include <RmlUi/Core/EventListener.h>

#include <optional>

namespace Rml {
class ElementDocument;
}

namespace NodeSpireUi {

// Placeholder options/settings screen reached from MainMenu's Options
// button. Just a Back button for now; real settings (audio/video/controls)
// are a follow-up session.
class OptionsScene final : public IScene, public Rml::EventListener {
  public:
    void onEnter(Rml::Context& context) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;

    void ProcessEvent(Rml::Event& event) override;

  private:
    Rml::ElementDocument* document_ = nullptr;
    SceneTransition pendingTransition_;
};

} // namespace NodeSpireUi
