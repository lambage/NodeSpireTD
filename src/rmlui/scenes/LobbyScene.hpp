#pragma once

#include "rmlui/IScene.hpp"

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Types.h>

class AudioEngine;

namespace Rml {
class ElementDocument;
class ElementFormControlInput;
}

namespace NodeSpireUi {

class LobbyScene final : public IScene, public Rml::EventListener {
  public:
    void onEnter(Rml::Context& context, AudioEngine& audio) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;

    void ProcessEvent(Rml::Event& event) override;

  private:
    void addListeners();
    void removeListeners();
    void enterParty(bool hosting);
    void leaveParty();
    void submitChat();
    void setStatus(const Rml::String& text);

    Rml::ElementDocument* document_ = nullptr;
    AudioEngine* audio_ = nullptr;
    SceneTransition pendingTransition_;
    bool inParty_ = false;
    bool hosting_ = false;
    bool ready_ = false;
    Rml::String chatHistoryRml_;
};

} // namespace NodeSpireUi
