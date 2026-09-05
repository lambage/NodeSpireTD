#include "rmlui/scenes/LobbyScene.hpp"

#include "AudioEngine.hpp"
#include "multiplayer/MultiplayerSession.hpp"
#include "multiplayer/PlayerProfileStore.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/StringUtilities.h>

namespace NodeSpireUi {

namespace {
constexpr const char* kHoverSound = "assets/audio/hover.ogg";
constexpr const char* kClickSound = "assets/audio/click.ogg";
constexpr unsigned short kPartyPort = 47321;
constexpr const char* kInteractiveIds[] = {
    "back-button", "solo-button", "host-button", "join-button", "ready-button", "leave-button", "chat-send-button",
};
} // namespace

LobbyScene::LobbyScene(multiplayer::MultiplayerSession& session, multiplayer::PlayerProfileStore& profileStore)
    : session_(session), profileStore_(profileStore) {}

void LobbyScene::onEnter(Rml::Context& context, AudioEngine& audio) {
    pendingTransition_ = std::nullopt;
    chatHistoryRml_.clear();
    renderedRosterRml_.clear();
    renderedStatus_.clear();
    renderedLaunchRml_.clear();
    renderedReady_.reset();
    renderedLaunchEnabled_.reset();
    audio_ = &audio;
    audio_->preload(kHoverSound, AudioChannel::Sfx);
    audio_->preload(kClickSound, AudioChannel::Sfx);

    document_ = context.LoadDocument("assets/ui/lobby/lobby.rml");
    if (document_) {
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

void LobbyScene::onExit(Rml::Context& context) {
    if (document_) {
        removeListeners();
        document_->Close();
        context.UnloadDocument(document_);
        document_ = nullptr;
    }
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
}

void LobbyScene::showParty() {
    inParty_ = true;

    if (Rml::Element* setup = document_->GetElementById("party-setup")) {
        setup->SetClass("hidden", true);
    }
    if (Rml::Element* party = document_->GetElementById("active-party")) {
        party->SetClass("hidden", false);
    }
    if (Rml::Element* briefing = document_->GetElementById("solo-briefing")) {
        briefing->SetClass("hidden", true);
    }
    if (Rml::Element* chatShell = document_->GetElementById("party-chat-shell")) {
        chatShell->SetClass("hidden", false);
    }
    if (Rml::Element* role = document_->GetElementById("party-role")) {
        role->SetInnerRML(session_.isHost() ? "Party leader" : "Party member");
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
    if (Rml::Element* briefing = document_->GetElementById("solo-briefing")) {
        briefing->SetClass("hidden", false);
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
    renderRoster(roster);
    if (session_.isClient() && !roster.members.empty()) {
        setStatus("Connected to party host.");
    }
    consumeChat();
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

SceneTransition LobbyScene::update(float /*dt*/) {
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
        if (!inParty_) {
            setStatus("Solo selected. PlayLevel is not available in the RmlUi executable yet.");
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
            setStatus("Online match selected. PlayLevel is not available in the RmlUi executable yet.");
        }
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
        session_.leaveParty();
        showPartySetup("Left the party.");
    } else if (id == "chat-send-button" && inParty_) {
        submitChat();
    }
}

} // namespace NodeSpireUi
