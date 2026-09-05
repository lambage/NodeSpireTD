#pragma once

#include "rmlui/IScene.hpp"

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Types.h>

#include <optional>

class AudioEngine;

namespace multiplayer {
class MultiplayerSession;
class PlayerProfileStore;
struct PartyRosterSnapshot;
}

namespace Rml {
class ElementDocument;
class ElementFormControlInput;
}

namespace NodeSpireUi {

class LobbyScene final : public IScene, public Rml::EventListener {
  public:
    LobbyScene(multiplayer::MultiplayerSession& session, multiplayer::PlayerProfileStore& profileStore);

    void onEnter(Rml::Context& context, AudioEngine& audio) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;
    bool handleShortcut(Rml::Input::KeyIdentifier key) override;

    void ProcessEvent(Rml::Event& event) override;

  private:
    void addListeners();
    void removeListeners();
    void showParty();
    void showPartySetup(const Rml::String& status);
    void refreshPartyView();
    void refreshLaunchButton(const multiplayer::PartyRosterSnapshot& roster);
    void renderRoster(const multiplayer::PartyRosterSnapshot& roster);
    void consumeChat();
    void appendChatLine(const Rml::String& author, const Rml::String& text, bool systemMessage, bool emote);
    bool savePlayerName();
    void submitChat();
    void setStatus(const Rml::String& text);

    multiplayer::MultiplayerSession& session_;
    multiplayer::PlayerProfileStore& profileStore_;
    Rml::ElementDocument* document_ = nullptr;
    AudioEngine* audio_ = nullptr;
    SceneTransition pendingTransition_;
    bool inParty_ = false;
    Rml::String chatHistoryRml_;
    Rml::String renderedRosterRml_;
    Rml::String renderedStatus_;
    Rml::String renderedLaunchRml_;
    std::optional<bool> renderedReady_;
    std::optional<bool> renderedLaunchEnabled_;
    bool removeChatFocusKey_ = false;
    bool normalizeSlashPrefix_ = false;
};

} // namespace NodeSpireUi
