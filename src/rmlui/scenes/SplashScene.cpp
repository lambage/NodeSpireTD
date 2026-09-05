#include "rmlui/scenes/SplashScene.hpp"

#include "AudioEngine.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Log.h>

namespace NodeSpireUi {

void SplashScene::onEnter(Rml::Context& context, AudioEngine& audio) {
    elapsedSeconds_ = 0.0f;

    document_ = context.LoadDocument("assets/ui/splash/splash.rml");
    if (document_) {
        document_->Show();
    } else {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load document: %s", "assets/ui/splash/splash.rml");
    }

    audio.play("assets/music/Heroic_Demise.mp3", AudioChannel::Music, true, 0.5f);
}

void SplashScene::onExit(Rml::Context& context) {
    if (document_) {
        document_->Close();
        context.UnloadDocument(document_);
        document_ = nullptr;
    }
}

SceneTransition SplashScene::update(float dt) {
    elapsedSeconds_ += dt;
    if (elapsedSeconds_ >= kMinimumSplashSeconds) {
        return SceneId::MainMenu;
    }
    return std::nullopt;
}

SceneTransition SplashScene::onKeyDown(Rml::Input::KeyIdentifier /*key*/) {
    return SceneId::MainMenu;
}

} // namespace NodeSpireUi
