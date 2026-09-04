#include "rmlui/scenes/LobbyScene.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>
#include <spdlog/spdlog.h>

namespace NodeSpireUi {

void LobbyScene::onEnter(Rml::Context& context) {
    pendingTransition_ = std::nullopt;

    document_ = context.LoadDocument("assets/ui/lobby/lobby.rml");
    if (document_) {
        document_->Show();
        if (Rml::Element* back = document_->GetElementById("back-button")) {
            back->AddEventListener(Rml::EventId::Click, this);
        }
    } else {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load document: %s", "assets/ui/lobby/lobby.rml");
    }
}

void LobbyScene::onExit(Rml::Context& context) {
    if (document_) {
        if (Rml::Element* back = document_->GetElementById("back-button")) {
            back->RemoveEventListener(Rml::EventId::Click, this);
        }
        document_->Close();
        context.UnloadDocument(document_);
        document_ = nullptr;
    }
}

SceneTransition LobbyScene::update(float /*dt*/) {
    SceneTransition transition = pendingTransition_;
    pendingTransition_ = std::nullopt;
    return transition;
}

void LobbyScene::ProcessEvent(Rml::Event& event) {
    Rml::Element* target = event.GetCurrentElement();
    if (target && target->GetId() == "back-button") {
        pendingTransition_ = SceneId::MainMenu;
    }
}

} // namespace NodeSpireUi
