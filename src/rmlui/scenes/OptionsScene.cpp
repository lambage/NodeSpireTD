#include "rmlui/scenes/OptionsScene.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>

#include <cstdio>

namespace NodeSpireUi {

namespace {
constexpr const char* kDocumentPath = "assets/ui/options/options.rml";

std::string percentLabel(float value01) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(value01 * 100.0f + 0.5f));
    return buffer;
}
} // namespace

void OptionsScene::onEnter(Rml::Context& context) {
    pendingTransition_ = std::nullopt;
    settings_ = settingsManager_.loadOrCreateDefaults();

    document_ = context.LoadDocument(kDocumentPath);
    if (document_) {
        document_->Show();
        populateControlsFromSettings();
        addListeners();
    } else {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load document: %s", kDocumentPath);
    }
}

void OptionsScene::onExit(Rml::Context& context) {
    if (document_) {
        removeListeners();
        document_->Close();
        context.UnloadDocument(document_);
        document_ = nullptr;
    }
}

SceneTransition OptionsScene::update(float /*dt*/) {
    SceneTransition transition = pendingTransition_;
    pendingTransition_ = std::nullopt;
    return transition;
}

void OptionsScene::populateControlsFromSettings() {
    if (Rml::Element* el = document_->GetElementById("fullscreen-checkbox")) {
        if (settings_.fullscreen) {
            el->SetAttribute("checked", true);
        }
    }
    if (Rml::Element* el = document_->GetElementById("vsync-checkbox")) {
        if (settings_.vSyncEnabled) {
            el->SetAttribute("checked", true);
        }
    }
    if (Rml::Element* el = document_->GetElementById("quality-slider")) {
        el->SetAttribute("value", settings_.graphicsQuality);
    }
    setValueLabel("quality-value", std::to_string(settings_.graphicsQuality));

    if (Rml::Element* el = document_->GetElementById("master-volume-slider")) {
        el->SetAttribute("value", settings_.masterVolume);
    }
    setValueLabel("master-volume-value", percentLabel(settings_.masterVolume));

    if (Rml::Element* el = document_->GetElementById("music-volume-slider")) {
        el->SetAttribute("value", settings_.musicVolume);
    }
    setValueLabel("music-volume-value", percentLabel(settings_.musicVolume));

    if (Rml::Element* el = document_->GetElementById("sfx-volume-slider")) {
        el->SetAttribute("value", settings_.sfxVolume);
    }
    setValueLabel("sfx-volume-value", percentLabel(settings_.sfxVolume));

    if (Rml::Element* el = document_->GetElementById("mute-unfocused-checkbox")) {
        if (settings_.muteWhenUnfocused) {
            el->SetAttribute("checked", true);
        }
    }
}

void OptionsScene::addListeners() {
    static constexpr const char* kIds[] = {
        "fullscreen-checkbox", "vsync-checkbox",    "quality-slider",         "master-volume-slider",
        "music-volume-slider", "sfx-volume-slider", "mute-unfocused-checkbox",
        "apply-button",        "back-button",
    };
    for (const char* id : kIds) {
        if (Rml::Element* el = document_->GetElementById(id)) {
            el->AddEventListener(Rml::EventId::Change, this);
            el->AddEventListener(Rml::EventId::Click, this);
        }
    }
}

void OptionsScene::removeListeners() {
    static constexpr const char* kIds[] = {
        "fullscreen-checkbox", "vsync-checkbox",    "quality-slider",         "master-volume-slider",
        "music-volume-slider", "sfx-volume-slider", "mute-unfocused-checkbox",
        "apply-button",        "back-button",
    };
    for (const char* id : kIds) {
        if (Rml::Element* el = document_->GetElementById(id)) {
            el->RemoveEventListener(Rml::EventId::Change, this);
            el->RemoveEventListener(Rml::EventId::Click, this);
        }
    }
}

void OptionsScene::setValueLabel(const std::string& labelId, const std::string& text) {
    if (Rml::Element* label = document_->GetElementById(labelId)) {
        label->SetInnerRML(text);
    }
}

void OptionsScene::ProcessEvent(Rml::Event& event) {
    Rml::Element* target = event.GetCurrentElement();
    if (!target) {
        return;
    }
    const Rml::String id = target->GetId();

    if (event == Rml::EventId::Click) {
        if (id == "apply-button") {
            if (!settingsManager_.save(settings_)) {
                Rml::Log::Message(Rml::Log::LT_WARNING, "Failed to save settings");
            }
        } else if (id == "back-button") {
            pendingTransition_ = SceneId::MainMenu;
        }
        return;
    }

    if (event != Rml::EventId::Change) {
        return;
    }

    if (id == "fullscreen-checkbox") {
        settings_.fullscreen = event.GetParameter<bool>("checked", false);
    } else if (id == "vsync-checkbox") {
        settings_.vSyncEnabled = event.GetParameter<bool>("checked", false);
    } else if (id == "mute-unfocused-checkbox") {
        settings_.muteWhenUnfocused = event.GetParameter<bool>("checked", false);
    } else if (id == "quality-slider") {
        settings_.graphicsQuality = static_cast<int>(event.GetParameter<float>("value", 0.0f) + 0.5f);
        setValueLabel("quality-value", std::to_string(settings_.graphicsQuality));
    } else if (id == "master-volume-slider") {
        settings_.masterVolume = event.GetParameter<float>("value", 0.0f);
        setValueLabel("master-volume-value", percentLabel(settings_.masterVolume));
    } else if (id == "music-volume-slider") {
        settings_.musicVolume = event.GetParameter<float>("value", 0.0f);
        setValueLabel("music-volume-value", percentLabel(settings_.musicVolume));
    } else if (id == "sfx-volume-slider") {
        settings_.sfxVolume = event.GetParameter<float>("value", 0.0f);
        setValueLabel("sfx-volume-value", percentLabel(settings_.sfxVolume));
    }
}

} // namespace NodeSpireUi

