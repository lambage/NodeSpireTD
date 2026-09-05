#include "rmlui/scenes/LobbyScene.hpp"

#include "AudioEngine.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>
#include <spdlog/spdlog.h>

namespace NodeSpireUi {

namespace {
constexpr const char* kHoverSound = "assets/audio/hover.ogg";
constexpr const char* kClickSound = "assets/audio/click.ogg";
} // namespace

void LobbyScene::onEnter(Rml::Context& context, AudioEngine& audio) {
    pendingTransition_ = std::nullopt;
    audio_ = &audio;
    audio_->preload(kHoverSound, AudioChannel::Sfx);
    audio_->preload(kClickSound, AudioChannel::Sfx);

    document_ = context.LoadDocument("assets/ui/lobby/lobby.rml");
    if (document_) {
        document_->Show();
        if (Rml::Element* back = document_->GetElementById("back-button")) {
            back->AddEventListener(Rml::EventId::Click, this);
            back->AddEventListener(Rml::EventId::Mouseover, this);
        }
    } else {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load document: %s", "assets/ui/lobby/lobby.rml");
    }
}

void LobbyScene::onExit(Rml::Context& context) {
    if (document_) {
        if (Rml::Element* back = document_->GetElementById("back-button")) {
            back->RemoveEventListener(Rml::EventId::Click, this);
            back->RemoveEventListener(Rml::EventId::Mouseover, this);
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
    if (!target || target->GetId() != "back-button") {
        return;
    }

    if (event == Rml::EventId::Mouseover) {
        if (audio_) {
            audio_->play(kHoverSound, AudioChannel::Sfx);
        }
        return;
    }

    if (audio_) {
        audio_->play(kClickSound, AudioChannel::Sfx);
    }
    pendingTransition_ = SceneId::MainMenu;
}

} // namespace NodeSpireUi
