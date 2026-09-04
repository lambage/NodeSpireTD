#include "rmlui/scenes/MainMenuScene.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Log.h>

namespace NodeSpireUi {

void MainMenuScene::onEnter(Rml::Context& context) {
    document_ = context.LoadDocument(NODESPIRE_ASSET_ROOT "/ui/mainmenu/mainmenu.rml");
    if (document_) {
        document_->Show();
    } else {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load document: %s", NODESPIRE_ASSET_ROOT "/ui/mainmenu/mainmenu.rml");
    }
}

void MainMenuScene::onExit(Rml::Context& context) {
    if (document_) {
        document_->Close();
        context.UnloadDocument(document_);
        document_ = nullptr;
    }
}

SceneTransition MainMenuScene::update(float /*dt*/) {
    return std::nullopt;
}

} // namespace NodeSpireUi
