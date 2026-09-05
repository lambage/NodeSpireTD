#include "rmlui/scenes/LobbyScene.hpp"

#include "AudioEngine.hpp"
#include "multiplayer/MultiplayerSession.hpp"
#include "multiplayer/PlayerProfileStore.hpp"
#include "scenes/TowerLoadController.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Lua/Interpreter.h>

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>

namespace NodeSpireUi {

namespace {
constexpr const char* kHoverSound = "assets/audio/hover.ogg";
constexpr const char* kClickSound = "assets/audio/click.ogg";
constexpr unsigned short kPartyPort = 47321;
constexpr const char* kLevelCatalogPath = "assets/levels/catalog.json";
constexpr std::size_t kVisibleLevelCards = 3;
constexpr const char* kInteractiveIds[] = {
    "back-button",          "solo-button",       "rejoin-button",      "host-button",        "join-button",
    "ready-button",         "leave-button",      "chat-send-button",   "choose-level-button",
    "level-previous-button", "level-next-button", "level-confirm-button", "level-cancel-button",
};
} // namespace

LobbyScene::LobbyScene(multiplayer::MultiplayerSession& session, multiplayer::PlayerProfileStore& profileStore,
                       PlayLevelLaunchConfig& playLevelLaunchConfig)
    : session_(session), profileStore_(profileStore), playLevelLaunchConfig_(playLevelLaunchConfig) {}

void LobbyScene::onEnter(Rml::Context& context, AudioEngine& audio) {
    pendingTransition_ = std::nullopt;
    chatHistoryRml_.clear();
    renderedRosterRml_.clear();
    renderedStatus_.clear();
    renderedLaunchRml_.clear();
    renderedReady_.reset();
    renderedLaunchEnabled_.reset();
    levels_.clear();
    selectedLevelIndex_ = 0;
    pendingLevelIndex_ = 0;
    carouselStartIndex_ = 0;
    audio_ = &audio;
    audio_->preload(kHoverSound, AudioChannel::Sfx);
    audio_->preload(kClickSound, AudioChannel::Sfx);

    towerCatalog_ = std::make_unique<TowerLoadController>(Rml::Lua::Interpreter::GetLuaState());
    towerCatalog_->discoverTowerArchetypesInDirectory("assets/models/towers");
    const bool loadoutConfigured = playLevelLaunchConfig_.towerLoadoutConfigured;
    selectedTowerIds_ = playLevelLaunchConfig_.towerLoadoutIds;
    towerCatalog_->setLoadoutIds(selectedTowerIds_);
    selectedTowerIds_ = towerCatalog_->loadoutIds();
    if (!loadoutConfigured) {
        std::vector<std::string> towerIds;
        towerIds.reserve(towerCatalog_->archetypes().size());
        for (const auto& [towerId, tower] : towerCatalog_->archetypes()) {
            (void)tower;
            towerIds.push_back(towerId);
        }
        std::sort(towerIds.begin(), towerIds.end());
        selectedTowerIds_.assign(towerIds.begin(), towerIds.begin() + std::min<std::size_t>(towerIds.size(), 5));
    }
    playLevelLaunchConfig_.towerLoadoutIds = selectedTowerIds_;
    playLevelLaunchConfig_.towerLoadoutConfigured = true;

    document_ = context.LoadDocument("assets/ui/lobby/lobby.rml");
    if (document_) {
        loadLevelCatalog();
        renderSelectedLevel();
        renderLoadout();
        document_->Show();
        addListeners();
        if (auto* playerName =
                rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document_->GetElementById("player-name"))) {
            playerName->SetValue(profileStore_.profile().displayName);
        }
        if (session_.isInParty()) {
            showParty();
        } else {
            showPartySetup("Choose solo play or form a party.");
        }
    } else {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load document: %s", "assets/ui/lobby/lobby.rml");
    }
}

void LobbyScene::renderLoadout() {
    if (!document_ || !towerCatalog_) {
        return;
    }

    for (Rml::Element* card : towerCardElements_) {
        card->RemoveEventListener(Rml::EventId::Click, this);
        card->RemoveEventListener(Rml::EventId::Mouseover, this);
    }
    towerCardElements_.clear();

    std::vector<const TowerArchetype*> towers;
    towers.reserve(towerCatalog_->archetypes().size());
    for (const auto& [towerId, tower] : towerCatalog_->archetypes()) {
        (void)towerId;
        towers.push_back(&tower);
    }
    std::sort(towers.begin(), towers.end(), [](const TowerArchetype* left, const TowerArchetype* right) {
        return left->displayName < right->displayName;
    });

    Rml::String inventoryRml;
    for (const TowerArchetype* tower : towers) {
        const bool selected = std::find(selectedTowerIds_.begin(), selectedTowerIds_.end(), tower->id) !=
                              selectedTowerIds_.end();
        inventoryRml += "<button id=\"tower-card-" + Rml::StringUtilities::EncodeRml(tower->id) +
                        "\" class=\"tower-card" + (selected ? " is-selected" : "") + "\">";
        if (!tower->previewImagePath.empty()) {
            inventoryRml += "<img class=\"tower-card-image\" src=\"" +
                            Rml::StringUtilities::EncodeRml(tower->previewImagePath) + "\"/>";
        } else {
            inventoryRml += "<span class=\"tower-card-glyph\">T</span>";
        }
        inventoryRml += "<span class=\"tower-card-copy\"><span class=\"tower-card-name\">" +
                        Rml::StringUtilities::EncodeRml(tower->displayName) +
                        "</span><span class=\"tower-card-cost\">$" + std::to_string(tower->cost) +
                        "</span></span></button>";
    }
    if (Rml::Element* inventory = document_->GetElementById("tower-inventory")) {
        inventory->SetInnerRML(inventoryRml);
    }

    Rml::String slotsRml;
    for (std::size_t slot = 0; slot < 5; ++slot) {
        slotsRml += "<div class=\"loadout-slot";
        if (slot < selectedTowerIds_.size()) {
            slotsRml += " filled\"><span class=\"slot-number\">" + std::to_string(slot + 1) + "</span><span>";
            const TowerArchetype* tower = towerCatalog_->findArchetype(selectedTowerIds_[slot]);
            slotsRml += Rml::StringUtilities::EncodeRml(tower ? tower->displayName : selectedTowerIds_[slot]);
            slotsRml += "</span>";
        } else {
            slotsRml += "\"><span class=\"slot-number\">" + std::to_string(slot + 1) +
                        "</span><span>Empty slot</span>";
        }
        slotsRml += "</div>";
    }
    if (Rml::Element* slots = document_->GetElementById("loadout-slots")) {
        slots->SetInnerRML(slotsRml);
    }
    if (Rml::Element* count = document_->GetElementById("loadout-count")) {
        count->SetInnerRML(std::to_string(selectedTowerIds_.size()) + " / 5");
    }

    for (const TowerArchetype* tower : towers) {
        if (Rml::Element* card = document_->GetElementById("tower-card-" + tower->id)) {
            card->AddEventListener(Rml::EventId::Click, this);
            card->AddEventListener(Rml::EventId::Mouseover, this);
            towerCardElements_.push_back(card);
        }
    }
}

void LobbyScene::toggleLoadoutTower(const std::string& towerId) {
    const auto selected = std::find(selectedTowerIds_.begin(), selectedTowerIds_.end(), towerId);
    if (selected != selectedTowerIds_.end()) {
        selectedTowerIds_.erase(selected);
    } else if (selectedTowerIds_.size() < 5) {
        selectedTowerIds_.push_back(towerId);
    } else {
        setStatus("Your loadout already has five towers. Remove one before adding another.");
        return;
    }
    playLevelLaunchConfig_.towerLoadoutIds = selectedTowerIds_;
    playLevelLaunchConfig_.towerLoadoutConfigured = true;
    renderLoadout();
}

bool LobbyScene::loadLevelCatalog() {
    std::ifstream input(kLevelCatalogPath);
    if (!input) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to open level catalog: %s", kLevelCatalogPath);
        return false;
    }

    try {
        const nlohmann::json catalog = nlohmann::json::parse(input);
        const std::string defaultLevelId = catalog.value("defaultLevel", std::string{});
        for (const auto& item : catalog.at("levels")) {
            LevelEntry level{
                item.at("id").get<std::string>(),          item.at("name").get<std::string>(),
                item.value("subtitle", item.at("name").get<std::string>()),
            item.value("description", std::string{}), item.value("threat", std::string{"UNKNOWN"}),
            item.value("players", std::string{"1-4"}), item.value("waves", std::string{"--"}),
            item.value("thumbnail", std::string{}),   item.value("mapAsset", std::string{}),
            item.value("startModel", std::string{}),
            item.value("endModel", std::string{}),
            item.value("animatedTemplateModels", std::vector<std::string>{}),
            };
            if (!level.id.empty() && !level.name.empty()) {
                levels_.push_back(std::move(level));
            }
        }

        const auto defaultLevel = std::find_if(levels_.begin(), levels_.end(), [&defaultLevelId](const LevelEntry& level) {
            return level.id == defaultLevelId;
        });
        if (defaultLevel != levels_.end()) {
            selectedLevelIndex_ = static_cast<std::size_t>(std::distance(levels_.begin(), defaultLevel));
        }
        pendingLevelIndex_ = selectedLevelIndex_;
        return !levels_.empty();
    } catch (const std::exception& error) {
        levels_.clear();
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to parse level catalog %s: %s", kLevelCatalogPath, error.what());
        return false;
    }
}

void LobbyScene::renderSelectedLevel() {
    if (!document_ || levels_.empty()) {
        return;
    }

    const LevelEntry& level = levels_[selectedLevelIndex_];
    if (Rml::Element* name = document_->GetElementById("selected-level-name")) {
        name->SetInnerRML(Rml::StringUtilities::EncodeRml(level.subtitle));
    }
    if (Rml::Element* description = document_->GetElementById("selected-level-description")) {
        description->SetInnerRML(Rml::StringUtilities::EncodeRml(level.description));
    }
    if (Rml::Element* threat = document_->GetElementById("selected-level-threat")) {
        threat->SetInnerRML(Rml::StringUtilities::EncodeRml(level.threat));
    }
    if (Rml::Element* players = document_->GetElementById("selected-level-players")) {
        players->SetInnerRML(Rml::StringUtilities::EncodeRml(level.players));
    }
    if (Rml::Element* waves = document_->GetElementById("selected-level-waves")) {
        waves->SetInnerRML(Rml::StringUtilities::EncodeRml(level.waves));
    }
}

void LobbyScene::renderLevelCarousel() {
    if (!document_) {
        return;
    }

    for (Rml::Element* card : levelCardElements_) {
        card->RemoveEventListener(Rml::EventId::Click, this);
        card->RemoveEventListener(Rml::EventId::Mouseover, this);
    }
    levelCardElements_.clear();

    Rml::String cardsRml;
    const std::size_t end = std::min(carouselStartIndex_ + kVisibleLevelCards, levels_.size());
    for (std::size_t index = carouselStartIndex_; index < end; ++index) {
        const LevelEntry& level = levels_[index];
        cardsRml += "<button id=\"level-card-" + std::to_string(index) + "\" class=\"level-card";
        if (index == pendingLevelIndex_) {
            cardsRml += " is-selected";
        }
        cardsRml += "\"><img class=\"level-thumbnail\" src=\"" + Rml::StringUtilities::EncodeRml(level.thumbnail) +
                    "\"/><span class=\"level-card-copy\"><span class=\"level-card-name\">" +
                    Rml::StringUtilities::EncodeRml(level.name) +
                    "</span><span class=\"level-card-description\">" +
                    Rml::StringUtilities::EncodeRml(level.description) +
                    "</span><span class=\"level-card-meta\">" + Rml::StringUtilities::EncodeRml(level.threat) +
                    " THREAT  /  " + Rml::StringUtilities::EncodeRml(level.waves) + " WAVES</span></span></button>";
    }

    if (Rml::Element* track = document_->GetElementById("level-carousel-track")) {
        track->SetInnerRML(cardsRml);
        for (std::size_t index = carouselStartIndex_; index < end; ++index) {
            if (Rml::Element* card = document_->GetElementById("level-card-" + std::to_string(index))) {
                card->AddEventListener(Rml::EventId::Click, this);
                card->AddEventListener(Rml::EventId::Mouseover, this);
                levelCardElements_.push_back(card);
            }
        }
    }

    if (Rml::Element* previous = document_->GetElementById("level-previous-button")) {
        previous->SetClass("is-disabled", carouselStartIndex_ == 0);
    }
    if (Rml::Element* next = document_->GetElementById("level-next-button")) {
        next->SetClass("is-disabled", end >= levels_.size());
    }
}

void LobbyScene::openLevelSelector() {
    if (levels_.empty()) {
        setStatus("No levels are available.");
        return;
    }
    pendingLevelIndex_ = selectedLevelIndex_;
    carouselStartIndex_ = pendingLevelIndex_ > 0 ? pendingLevelIndex_ - 1 : 0;
    if (levels_.size() > kVisibleLevelCards) {
        carouselStartIndex_ = std::min(carouselStartIndex_, levels_.size() - kVisibleLevelCards);
    }
    renderLevelCarousel();
    if (Rml::Element* selector = document_->GetElementById("level-selector")) {
        selector->SetClass("hidden", false);
    }
}

void LobbyScene::closeLevelSelector(bool commitSelection) {
    if (commitSelection && !levels_.empty()) {
        selectedLevelIndex_ = pendingLevelIndex_;
        renderSelectedLevel();
        setStatus("Selected " + levels_[selectedLevelIndex_].name + ".");
    }
    if (Rml::Element* selector = document_->GetElementById("level-selector")) {
        selector->SetClass("hidden", true);
    }
}

void LobbyScene::onExit(Rml::Context& context) {
    if (document_) {
        removeListeners();
        levelCardElements_.clear();
        towerCardElements_.clear();
        document_->Close();
        context.UnloadDocument(document_);
        document_ = nullptr;
    }
    towerCatalog_.reset();
}

void LobbyScene::addListeners() {
    for (const char* id : kInteractiveIds) {
        if (Rml::Element* element = document_->GetElementById(id)) {
            element->AddEventListener(Rml::EventId::Click, this);
            element->AddEventListener(Rml::EventId::Mouseover, this);
        }
    }
    if (Rml::Element* chatInput = document_->GetElementById("chat-input")) {
        chatInput->AddEventListener(Rml::EventId::Change, this);
    }
}

void LobbyScene::removeListeners() {
    for (const char* id : kInteractiveIds) {
        if (Rml::Element* element = document_->GetElementById(id)) {
            element->RemoveEventListener(Rml::EventId::Click, this);
            element->RemoveEventListener(Rml::EventId::Mouseover, this);
        }
    }
    if (Rml::Element* chatInput = document_->GetElementById("chat-input")) {
        chatInput->RemoveEventListener(Rml::EventId::Change, this);
    }
    for (Rml::Element* card : levelCardElements_) {
        card->RemoveEventListener(Rml::EventId::Click, this);
        card->RemoveEventListener(Rml::EventId::Mouseover, this);
    }
    for (Rml::Element* card : towerCardElements_) {
        card->RemoveEventListener(Rml::EventId::Click, this);
        card->RemoveEventListener(Rml::EventId::Mouseover, this);
    }
}

void LobbyScene::showParty() {
    inParty_ = true;

    if (Rml::Element* setup = document_->GetElementById("party-setup")) {
        setup->SetClass("hidden", true);
    }
    if (Rml::Element* party = document_->GetElementById("active-party")) {
        party->SetClass("hidden", false);
    }
    if (Rml::Element* chatShell = document_->GetElementById("party-chat-shell")) {
        chatShell->SetClass("hidden", false);
    }
    if (Rml::Element* role = document_->GetElementById("party-role")) {
        role->SetInnerRML(session_.isHost() ? "Party leader" : "Party member");
    }
    if (Rml::Element* leave = document_->GetElementById("leave-button")) {
        leave->SetInnerRML(session_.isHost() ? "Disband party" : "Leave party");
    }
    if (Rml::Element* chat = document_->GetElementById("chat-messages")) {
        chat->SetInnerRML(chatHistoryRml_);
    }
    setStatus(session_.isHost() ? "Party created. Listening on port 47321."
                                : "Connecting to party host on port 47321...");
    refreshPartyView();
}

void LobbyScene::showPartySetup(const Rml::String& status) {
    inParty_ = false;
    chatHistoryRml_.clear();

    if (Rml::Element* launchButton = document_->GetElementById("solo-button")) {
        renderedLaunchRml_ =
            "<span class=\"button-label\">Start solo</span>"
            "<span class=\"button-note\">Immediate deployment</span>";
        launchButton->SetInnerRML(renderedLaunchRml_);
        launchButton->RemoveAttribute("disabled");
        renderedLaunchEnabled_ = true;
    }

    if (Rml::Element* setup = document_->GetElementById("party-setup")) {
        setup->SetClass("hidden", false);
    }
    if (Rml::Element* party = document_->GetElementById("active-party")) {
        party->SetClass("hidden", true);
    }
    if (Rml::Element* chatShell = document_->GetElementById("party-chat-shell")) {
        chatShell->SetClass("hidden", true);
    }
    setStatus(status);
}

void LobbyScene::renderRoster(const multiplayer::PartyRosterSnapshot& roster) {
    Rml::String rosterRml;
    bool localReady = false;

    for (const auto& member : roster.members) {
        const bool isLocal = member.playerId == session_.localPlayerId();
        localReady |= isLocal && member.ready;
        rosterRml += "<div class=\"roster-row";
        if (isLocal) {
            rosterRml += " local-player";
        }
        rosterRml += "\"><span class=\"player-name\">";
        rosterRml += Rml::StringUtilities::EncodeRml(member.displayName);
        if (member.isHost) {
            rosterRml += " <span class=\"leader-tag\">LEADER</span>";
        }
        rosterRml += "</span><span class=\"player-state";
        if (member.ready) {
            rosterRml += " is-ready";
        }
        rosterRml += "\">";
        rosterRml += member.ready ? "READY" : "NOT READY";
        rosterRml += "</span></div>";
    }

    for (std::size_t slot = roster.members.size(); slot < roster.capacity; ++slot) {
        rosterRml += "<div class=\"roster-row empty-slot\"><span>Open slot</span><span>WAITING</span></div>";
    }

    if (rosterRml != renderedRosterRml_) {
        if (Rml::Element* rosterElement = document_->GetElementById("party-roster")) {
            rosterElement->SetInnerRML(rosterRml);
            renderedRosterRml_ = rosterRml;
        }
    }
    if (renderedReady_ != localReady) {
        if (Rml::Element* readyButton = document_->GetElementById("ready-button")) {
            readyButton->SetClass("is-ready", localReady);
            readyButton->SetInnerRML(localReady ? "Ready" : "Not ready");
            renderedReady_ = localReady;
        }
    }
}

void LobbyScene::refreshLaunchButton(const multiplayer::PartyRosterSnapshot& roster) {
    Rml::Element* launchButton = document_->GetElementById("solo-button");
    if (!launchButton) {
        return;
    }

    const bool isHost = session_.isHost();
    const bool allReady = !roster.members.empty() &&
                          std::all_of(roster.members.begin(), roster.members.end(),
                                      [](const multiplayer::PartyMemberState& member) { return member.ready; });
    const bool canStart = isHost && allReady;
    const Rml::String launchRml =
        "<span class=\"button-label\">" + Rml::String(isHost ? "Start online match" : "Waiting for host") +
        "</span>"
        "<span class=\"button-note\">" +
        Rml::String(isHost ? (allReady ? "Party ready for deployment" : "Waiting for all players to ready up")
                           : "The party leader will start the match") +
        "</span>";
    if (renderedLaunchRml_ != launchRml) {
        launchButton->SetInnerRML(launchRml);
        renderedLaunchRml_ = launchRml;
    }
    if (renderedLaunchEnabled_ != canStart) {
        if (canStart) {
            launchButton->RemoveAttribute("disabled");
        } else {
            launchButton->SetAttribute("disabled", "");
        }
        renderedLaunchEnabled_ = canStart;
    }
}

void LobbyScene::appendChatLine(const Rml::String& author, const Rml::String& text, bool systemMessage, bool emote) {
    if (systemMessage) {
        chatHistoryRml_ += "<p class=\"system-message\">";
    } else if (emote) {
        chatHistoryRml_ += "<p class=\"emote-message\">";
    } else {
        chatHistoryRml_ += "<p class=\"chat-message\">";
    }
    if (!systemMessage && !emote) {
        chatHistoryRml_ += "<span class=\"chat-author\">";
        chatHistoryRml_ += Rml::StringUtilities::EncodeRml(author);
        chatHistoryRml_ += "</span> ";
    }
    if (emote) {
        chatHistoryRml_ += "<em>";
    }
    chatHistoryRml_ += Rml::StringUtilities::EncodeRml(text);
    if (emote) {
        chatHistoryRml_ += "</em>";
    }
    chatHistoryRml_ += "</p>";
}

void LobbyScene::consumeChat() {
    bool changed = false;
    for (const auto& message : session_.consumeChatMessages()) {
        appendChatLine(message.displayName, message.text, message.playerId == 0, message.isEmote);
        changed = true;
    }
    for (const auto& error : session_.consumeChatErrors()) {
        appendChatLine("System", error, true, false);
        changed = true;
    }
    if (changed) {
        if (Rml::Element* chat = document_->GetElementById("chat-messages")) {
            chat->SetInnerRML(chatHistoryRml_);
        }
    }
}

void LobbyScene::refreshPartyView() {
    if (!document_ || !session_.isInParty()) {
        return;
    }
    const auto roster = session_.roster();
    refreshLaunchButton(roster);
    refreshRejoinButton();
    renderRoster(roster);
    if (session_.isClient() && !roster.members.empty()) {
        setStatus("Connected to party host.");
    }
    consumeChat();
}

void LobbyScene::refreshRejoinButton() {
    if (Rml::Element* rejoin = document_->GetElementById("rejoin-button")) {
        rejoin->SetClass("hidden", !(session_.isClient() && session_.activeMatch().has_value()));
    }
}

bool LobbyScene::savePlayerName() {
    auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document_->GetElementById("player-name"));
    if (!input || !profileStore_.setDisplayName(input->GetValue())) {
        setStatus("Enter a player name between 1 and 32 characters.");
        return false;
    }
    input->SetValue(profileStore_.profile().displayName);
    return true;
}

void LobbyScene::submitChat() {
    auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document_->GetElementById("chat-input"));
    if (!input) {
        return;
    }

    const Rml::String message = input->GetValue();
    if (message.empty()) {
        return;
    }

    if (session_.sendChatMessage(message)) {
        input->SetValue("");
    } else if (session_.isClient()) {
        setStatus("Message could not be sent.");
    }
}

void LobbyScene::setStatus(const Rml::String& text) {
    if (renderedStatus_ == text) {
        return;
    }
    if (Rml::Element* status = document_->GetElementById("lobby-status")) {
        status->SetInnerRML(text);
        renderedStatus_ = text;
    }
}

void LobbyScene::configurePlayLevelLaunch(const LevelEntry& level) {
    std::vector<std::string> towerLoadoutIds = playLevelLaunchConfig_.towerLoadoutIds;
    const bool towerLoadoutConfigured = playLevelLaunchConfig_.towerLoadoutConfigured;
    playLevelLaunchConfig_ = {level.id, level.name, level.mapAsset, level.startModel, level.endModel,
                              level.animatedTemplateModels};
    playLevelLaunchConfig_.towerLoadoutIds = std::move(towerLoadoutIds);
    playLevelLaunchConfig_.towerLoadoutConfigured = towerLoadoutConfigured;
}

bool LobbyScene::configurePlayLevelLaunch(const multiplayer::PartyMatchStartAnnouncement& announcement) {
    const auto level = std::find_if(levels_.begin(), levels_.end(), [&announcement](const LevelEntry& candidate) {
        return candidate.id == announcement.levelId ||
               candidate.mapAsset == announcement.levelAssetPath || candidate.name == announcement.levelName;
    });
    if (level == levels_.end()) {
        setStatus("The host selected a level that is not in the local catalog.");
        return false;
    }
    configurePlayLevelLaunch(*level);
    return true;
}

SceneTransition LobbyScene::update(float /*dt*/) {
    if (leavePartyPending_) {
        leavePartyPending_ = false;
        const bool disbanding = session_.isHost();
        session_.leaveParty();
        showPartySetup(disbanding ? "Party disbanded." : "Left the party.");
    }

    if (auto* chatInput =
            rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document_->GetElementById("chat-input"))) {
        Rml::String value = chatInput->GetValue();
        if (removeChatFocusKey_ && !value.empty() && (value.front() == 't' || value.front() == 'T')) {
            value.erase(0, 1);
            chatInput->SetValue(value);
            chatInput->SetSelectionRange(static_cast<int>(value.size()), static_cast<int>(value.size()));
        }
        if (normalizeSlashPrefix_ && value.starts_with("//")) {
            value.erase(0, 1);
            chatInput->SetValue(value);
            chatInput->SetSelectionRange(static_cast<int>(value.size()), static_cast<int>(value.size()));
        }
    }
    removeChatFocusKey_ = false;
    normalizeSlashPrefix_ = false;

    if (inParty_ && !session_.isInParty()) {
        showPartySetup("Connection to the party ended.");
    } else if (session_.isInParty()) {
        if (!inParty_) {
            showParty();
        }
        refreshPartyView();
    }

    if (session_.isClient()) {
        if (const auto announcement = session_.consumeMatchStartAnnouncement()) {
            if (configurePlayLevelLaunch(*announcement)) {
                pendingTransition_ = SceneId::PlayLevel;
            }
        }
    }

    SceneTransition transition = pendingTransition_;
    pendingTransition_ = std::nullopt;
    return transition;
}

bool LobbyScene::handleShortcut(Rml::Input::KeyIdentifier key) {
    if (!document_ || !inParty_) {
        return false;
    }

    auto* chatInput = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document_->GetElementById("chat-input"));
    if (!chatInput || document_->GetContext()->GetFocusElement() == chatInput) {
        return false;
    }
    if (rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document_->GetContext()->GetFocusElement())) {
        return false;
    }

    if (key == Rml::Input::KI_T) {
        chatInput->Focus();
        removeChatFocusKey_ = true;
        return true;
    }
    if (key == Rml::Input::KI_OEM_2 || key == Rml::Input::KI_DIVIDE) {
        chatInput->Focus();
        chatInput->SetValue("/");
        chatInput->SetSelectionRange(1, 1);
        normalizeSlashPrefix_ = true;
        return true;
    }
    return false;
}

void LobbyScene::ProcessEvent(Rml::Event& event) {
    Rml::Element* target = event.GetCurrentElement();
    if (!target) {
        return;
    }

    const Rml::String& id = target->GetId();

    if (event == Rml::EventId::Change && id == "chat-input" && event.GetParameter<bool>("linebreak", false)) {
        submitChat();
        return;
    }

    if (event == Rml::EventId::Mouseover) {
        if (audio_ && !target->HasAttribute("disabled")) {
            audio_->play(kHoverSound, AudioChannel::Sfx);
        }
        return;
    }

    if (audio_) {
        audio_->play(kClickSound, AudioChannel::Sfx);
    }

    if (id == "back-button") {
        pendingTransition_ = SceneId::MainMenu;
    } else if (id == "solo-button") {
        if (levels_.empty()) {
            setStatus("No deployable levels are available.");
            return;
        }
        if (!inParty_) {
            const LevelEntry& level = levels_[selectedLevelIndex_];
            configurePlayLevelLaunch(level);
            pendingTransition_ = SceneId::PlayLevel;
            return;
        }

        const auto roster = session_.roster();
        const bool allReady = !roster.members.empty() &&
                              std::all_of(roster.members.begin(), roster.members.end(),
                                          [](const multiplayer::PartyMemberState& member) { return member.ready; });
        if (!session_.isHost()) {
            setStatus("Only the party leader can start the match.");
        } else if (!allReady) {
            setStatus("All players must be ready before the match can start.");
        } else {
            const LevelEntry& level = levels_[selectedLevelIndex_];
            if (session_.announceMatchStart(level.name, level.id, level.mapAsset)) {
                configurePlayLevelLaunch(level);
                pendingTransition_ = SceneId::PlayLevel;
            } else {
                setStatus("Could not start the online match.");
            }
        }
    } else if (id == "rejoin-button") {
        const auto& activeMatch = session_.activeMatch();
        if (activeMatch && configurePlayLevelLaunch(*activeMatch)) {
            pendingTransition_ = SceneId::PlayLevel;
        } else {
            setStatus("That match is no longer available.");
            refreshRejoinButton();
        }
    } else if (id == "choose-level-button") {
        openLevelSelector();
    } else if (id == "level-previous-button") {
        if (carouselStartIndex_ > 0) {
            --carouselStartIndex_;
            renderLevelCarousel();
        }
    } else if (id == "level-next-button") {
        if (carouselStartIndex_ + kVisibleLevelCards < levels_.size()) {
            ++carouselStartIndex_;
            renderLevelCarousel();
        }
    } else if (id == "level-confirm-button") {
        closeLevelSelector(true);
    } else if (id == "level-cancel-button") {
        closeLevelSelector(false);
    } else if (id.starts_with("level-card-")) {
        const std::size_t index = static_cast<std::size_t>(std::stoul(id.substr(11)));
        if (index < levels_.size()) {
            pendingLevelIndex_ = index;
            renderLevelCarousel();
        }
    } else if (id.starts_with("tower-card-")) {
        toggleLoadoutTower(id.substr(11));
    } else if (id == "host-button") {
        if (!savePlayerName()) {
            return;
        }
        const auto& profile = profileStore_.profile();
        if (session_.hostParty(kPartyPort, profile.displayName, profile.playerUuid)) {
            showParty();
        } else {
            setStatus("Could not create party. Port 47321 may already be in use.");
        }
    } else if (id == "join-button") {
        auto* address = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document_->GetElementById("join-address"));
        if (!savePlayerName()) {
            return;
        } else if (address && !address->GetValue().empty()) {
            const auto& profile = profileStore_.profile();
            if (session_.joinParty(address->GetValue(), kPartyPort, profile.displayName, profile.playerUuid)) {
                showParty();
            } else {
                setStatus("Could not connect to that host on port 47321.");
            }
        } else {
            setStatus("Enter a host address before joining.");
        }
    } else if (id == "ready-button" && inParty_) {
        bool localReady = false;
        for (const auto& member : session_.roster().members) {
            if (member.playerId == session_.localPlayerId()) {
                localReady = member.ready;
                break;
            }
        }
        if (!session_.setLocalReady(!localReady)) {
            setStatus("Ready state could not be updated.");
        }
    } else if (id == "leave-button") {
        leavePartyPending_ = true;
    } else if (id == "chat-send-button" && inParty_) {
        submitChat();
    }
}

} // namespace NodeSpireUi
