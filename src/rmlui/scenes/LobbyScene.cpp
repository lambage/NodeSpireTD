#include "rmlui/scenes/LobbyScene.hpp"

#include "AudioEngine.hpp"

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
constexpr const char* kInteractiveIds[] = {
    "back-button", "solo-button", "host-button", "join-button", "ready-button", "leave-button", "chat-send-button",
};
} // namespace

void LobbyScene::onEnter(Rml::Context& context, AudioEngine& audio) {
    pendingTransition_ = std::nullopt;
    audio_ = &audio;
    audio_->preload(kHoverSound, AudioChannel::Sfx);
    audio_->preload(kClickSound, AudioChannel::Sfx);

    document_ = context.LoadDocument("assets/ui/lobby/lobby.rml");
    if (document_) {
        document_->Show();
        addListeners();
        leaveParty();
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
}

void LobbyScene::removeListeners() {
    for (const char* id : kInteractiveIds) {
        if (Rml::Element* element = document_->GetElementById(id)) {
            element->RemoveEventListener(Rml::EventId::Click, this);
            element->RemoveEventListener(Rml::EventId::Mouseover, this);
        }
    }
}

void LobbyScene::enterParty(bool hosting) {
    inParty_ = true;
    hosting_ = hosting;
    ready_ = false;
    chatHistoryRml_ = "<p class=\"system-message\">Party channel opened.</p>";

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
        role->SetInnerRML(hosting_ ? "Party leader" : "Party member");
    }
    if (Rml::Element* rosterName = document_->GetElementById("local-player-name")) {
        rosterName->SetInnerRML(hosting_ ? "You <span class=\"leader-tag\">LEADER</span>" : "You");
    }
    if (Rml::Element* chat = document_->GetElementById("chat-messages")) {
        chat->SetInnerRML(chatHistoryRml_);
    }
    setStatus(hosting_ ? "Party created. Invite players when networking is connected."
                       : "Joined local party preview. Network transport is the next integration step.");
}

void LobbyScene::leaveParty() {
    inParty_ = false;
    hosting_ = false;
    ready_ = false;
    chatHistoryRml_.clear();

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
    setStatus("Choose solo play or form a party.");
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

    chatHistoryRml_ += "<p class=\"chat-message\"><span class=\"chat-author\">You</span> ";
    chatHistoryRml_ += Rml::StringUtilities::EncodeRml(message);
    chatHistoryRml_ += "</p>";
    if (Rml::Element* chat = document_->GetElementById("chat-messages")) {
        chat->SetInnerRML(chatHistoryRml_);
    }
    input->SetValue("");
}

void LobbyScene::setStatus(const Rml::String& text) {
    if (Rml::Element* status = document_->GetElementById("lobby-status")) {
        status->SetInnerRML(text);
    }
}

SceneTransition LobbyScene::update(float /*dt*/) {
    SceneTransition transition = pendingTransition_;
    pendingTransition_ = std::nullopt;
    return transition;
}

void LobbyScene::ProcessEvent(Rml::Event& event) {
    Rml::Element* target = event.GetCurrentElement();
    if (!target) {
        return;
    }

    const Rml::String& id = target->GetId();

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
        setStatus("Solo selected. PlayLevel is not available in the RmlUi executable yet.");
    } else if (id == "host-button") {
        enterParty(true);
    } else if (id == "join-button") {
        auto* address = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document_->GetElementById("join-address"));
        if (address && !address->GetValue().empty()) {
            enterParty(false);
        } else {
            setStatus("Enter a host address before joining.");
        }
    } else if (id == "ready-button" && inParty_) {
        ready_ = !ready_;
        target->SetClass("is-ready", ready_);
        target->SetInnerRML(ready_ ? "Ready" : "Not ready");
        if (Rml::Element* state = document_->GetElementById("local-player-state")) {
            state->SetClass("is-ready", ready_);
            state->SetInnerRML(ready_ ? "READY" : "NOT READY");
        }
    } else if (id == "leave-button") {
        leaveParty();
    } else if (id == "chat-send-button" && inParty_) {
        submitChat();
    }
}

} // namespace NodeSpireUi
