#include "rmlui/scenes/MainMenuScene.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi_Backend.h>
#include <spdlog/spdlog.h>

namespace NodeSpireUi {

void MainMenuScene::onEnter(Rml::Context& context) {
    pendingTransition_ = std::nullopt;

    document_ = context.LoadDocument("assets/ui/mainmenu/mainmenu.rml");
    if (document_) {
        document_->Show();
        if (Rml::Element* play = document_->GetElementById("play-button")) {
            play->AddEventListener(Rml::EventId::Click, this);
        }
        if (Rml::Element* options = document_->GetElementById("options-button")) {
            options->AddEventListener(Rml::EventId::Click, this);
        }
        if (Rml::Element* exit = document_->GetElementById("exit-button")) {
            exit->AddEventListener(Rml::EventId::Click, this);
        }
    } else {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load document: %s", "assets/ui/mainmenu/mainmenu.rml");
    }
}

void MainMenuScene::onExit(Rml::Context& context) {
    if (document_) {
        // Remove listeners before Close() so RmlUi's deferred document
        // cleanup doesn't call OnDetach on this soon-to-be-destroyed scene.
        if (Rml::Element* play = document_->GetElementById("play-button")) {
            play->RemoveEventListener(Rml::EventId::Click, this);
        }
        if (Rml::Element* options = document_->GetElementById("options-button")) {
            options->RemoveEventListener(Rml::EventId::Click, this);
        }
        if (Rml::Element* exit = document_->GetElementById("exit-button")) {
            exit->RemoveEventListener(Rml::EventId::Click, this);
        }
        document_->Close();
        context.UnloadDocument(document_);
        document_ = nullptr;
    }
}

SceneTransition MainMenuScene::update(float /*dt*/) {
    SceneTransition transition = pendingTransition_;
    pendingTransition_ = std::nullopt;
    return transition;
}

void MainMenuScene::ProcessEvent(Rml::Event& event) {
    Rml::Element* target = event.GetCurrentElement();
    if (!target) {
        return;
    }

    const Rml::String& id = target->GetId();
    if (id == "play-button") {
        pendingTransition_ = SceneId::Lobby;
    } else if (id == "options-button") {
        pendingTransition_ = SceneId::Options;
    } else if (id == "exit-button") {
        Backend::RequestExit();
    }
}

} // namespace NodeSpireUi

