#pragma once

#include "rmlui/IScene.hpp"

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Types.h>

#include <optional>
#include <memory>
#include <string>
#include <vector>

class AudioEngine;
class TowerLoadController;

namespace multiplayer {
class MultiplayerSession;
class PlayerProfileStore;
struct PartyMatchStartAnnouncement;
struct PartyRosterSnapshot;
}

namespace Rml {
class ElementDocument;
class ElementFormControlInput;
}

namespace NodeSpireUi {

class LobbyScene final : public IScene, public Rml::EventListener {
  public:
    LobbyScene(multiplayer::MultiplayerSession& session, multiplayer::PlayerProfileStore& profileStore,
           PlayLevelLaunchConfig& playLevelLaunchConfig);

    void onEnter(Rml::Context& context, AudioEngine& audio) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;
    bool handleShortcut(Rml::Input::KeyIdentifier key) override;

    void ProcessEvent(Rml::Event& event) override;

  private:
    struct LevelEntry {
        std::string id;
        std::string name;
        std::string subtitle;
        std::string description;
        std::string threat;
        std::string players;
        std::string waves;
        std::string thumbnail;
        std::string mapAsset;
        std::string startModel;
        std::string endModel;
        std::vector<std::string> animatedTemplateModels;
    };

    void addListeners();
    void removeListeners();
    bool loadLevelCatalog();
    void renderSelectedLevel();
    void renderLevelCarousel();
    void renderLoadout();
    void toggleLoadoutTower(const std::string& towerId);
    void openLevelSelector();
    void closeLevelSelector(bool commitSelection);
    void showParty();
    void showPartySetup(const Rml::String& status, const Rml::String& connectionNotice = {});
    void refreshPartyView();
    void refreshLaunchButton(const multiplayer::PartyRosterSnapshot& roster);
    void refreshRejoinButton();
    void renderRoster(const multiplayer::PartyRosterSnapshot& roster);
    void consumeChat();
    void appendChatLine(const Rml::String& author, const Rml::String& text, bool systemMessage, bool emote);
    bool savePlayerName();
    void submitChat();
    void setStatus(const Rml::String& text);
    void configurePlayLevelLaunch(const LevelEntry& level);
    bool configurePlayLevelLaunch(const multiplayer::PartyMatchStartAnnouncement& announcement);

    multiplayer::MultiplayerSession& session_;
    multiplayer::PlayerProfileStore& profileStore_;
    PlayLevelLaunchConfig& playLevelLaunchConfig_;
    Rml::ElementDocument* document_ = nullptr;
    AudioEngine* audio_ = nullptr;
    SceneTransition pendingTransition_;
    bool leavePartyPending_ = false;
    bool inParty_ = false;
    Rml::String chatHistoryRml_;
    Rml::String renderedRosterRml_;
    Rml::String renderedStatus_;
    Rml::String renderedLaunchRml_;
    std::optional<bool> renderedReady_;
    std::optional<bool> renderedLaunchEnabled_;
    std::vector<LevelEntry> levels_;
    std::size_t selectedLevelIndex_ = 0;
    std::size_t pendingLevelIndex_ = 0;
    std::size_t carouselStartIndex_ = 0;
    std::vector<Rml::Element*> levelCardElements_;
    std::vector<Rml::Element*> towerCardElements_;
    std::unique_ptr<TowerLoadController> towerCatalog_;
    std::vector<std::string> selectedTowerIds_;
    bool removeChatFocusKey_ = false;
    bool normalizeSlashPrefix_ = false;
};

} // namespace NodeSpireUi
