#include "rmlui/scenes/OptionsScene.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi_Backend.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <unordered_set>

namespace NodeSpireUi {

namespace {
constexpr const char* kDocumentPath = "assets/ui/options/options.rml";

std::string percentLabel(float value01) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(value01 * 100.0f + 0.5f));
    return buffer;
}

std::string displayModeLabel(const OptionsScene::DisplayModeOption& mode) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%d x %d @ %d Hz", mode.width, mode.height, mode.refreshRate);
    return buffer;
}

std::string displayModeValue(const OptionsScene::DisplayModeOption& mode) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%dx%dx%d", mode.width, mode.height, mode.refreshRate);
    return buffer;
}

bool parseDisplayModeValue(const Rml::String& value, OptionsScene::DisplayModeOption& outMode) {
    return std::sscanf(value.c_str(), "%dx%dx%d", &outMode.width, &outMode.height, &outMode.refreshRate) == 3;
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
        // TEMP DIAGNOSTIC: log click coordinates and dropdown geometry.
        document_->AddEventListener(Rml::EventId::Click, this, true);
    } else {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load document: %s", kDocumentPath);
    }
}

void OptionsScene::onExit(Rml::Context& context) {
    if (document_) {
        document_->RemoveEventListener(Rml::EventId::Click, this, true);
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
    if (Rml::Element* el = document_->GetElementById("exclusive-fullscreen-checkbox")) {
        if (settings_.exclusiveFullscreen) {
            el->SetAttribute("checked", true);
        }
    }
    if (Rml::Element* el = document_->GetElementById("vsync-checkbox")) {
        if (settings_.vSyncEnabled) {
            el->SetAttribute("checked", true);
        }
    }
    populateDisplayModeOptions();
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

void OptionsScene::populateDisplayModeOptions() {
    auto* select = document_->GetElementById("display-mode-select");
    auto* selectControl = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(select);
    if (!selectControl) {
        return;
    }
    selectControl->RemoveAll();

    displayModes_.clear();
    int count = 0;
    if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &count)) {
        std::unordered_set<long long> seen;
        for (int i = 0; i < count; ++i) {
            const SDL_DisplayMode* mode = modes[i];
            if (!mode || mode->w <= 0 || mode->h <= 0) {
                continue;
            }
            const int refreshRate = static_cast<int>(mode->refresh_rate + 0.5f);
            const long long key = (static_cast<long long>(mode->w) << 40) | (static_cast<long long>(mode->h) << 20) | refreshRate;
            if (seen.insert(key).second) {
                displayModes_.push_back({mode->w, mode->h, refreshRate});
            }
        }
        SDL_free(modes);
    }

    if (displayModes_.empty()) {
        displayModes_.push_back({settings_.displayWidth, settings_.displayHeight, settings_.refreshRate});
    }

    std::sort(displayModes_.begin(), displayModes_.end(), [](const DisplayModeOption& left, const DisplayModeOption& right) {
        if (left.width != right.width) {
            return left.width > right.width;
        }
        if (left.height != right.height) {
            return left.height > right.height;
        }
        return left.refreshRate > right.refreshRate;
    });

    int selectedIndex = 0;
    for (size_t i = 0; i < displayModes_.size(); ++i) {
        const DisplayModeOption& mode = displayModes_[i];
        selectControl->Add(displayModeLabel(mode), displayModeValue(mode));
        if (mode.width == settings_.displayWidth && mode.height == settings_.displayHeight &&
            mode.refreshRate == settings_.refreshRate) {
            selectedIndex = static_cast<int>(i);
        }
    }
    selectControl->SetSelection(selectedIndex);
}

void OptionsScene::addListeners() {
    static constexpr const char* kIds[] = {
        "fullscreen-checkbox", "exclusive-fullscreen-checkbox", "vsync-checkbox", "display-mode-select",
        "quality-slider",      "master-volume-slider",          "music-volume-slider", "sfx-volume-slider",
        "mute-unfocused-checkbox", "apply-button", "back-button",
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
        "fullscreen-checkbox", "exclusive-fullscreen-checkbox", "vsync-checkbox", "display-mode-select",
        "quality-slider",      "master-volume-slider",          "music-volume-slider", "sfx-volume-slider",
        "mute-unfocused-checkbox", "apply-button", "back-button",
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

    if (event == Rml::EventId::Click && event.GetPhase() == Rml::EventPhase::Capture) {
        Rml::Element* realTarget = event.GetTargetElement();
        const Rml::Vector2f mousePos(event.GetParameter("mouse_x", 0.0f), event.GetParameter("mouse_y", 0.0f));
        Rml::Log::Message(Rml::Log::LT_INFO, "[OptionsScene] click at (%.1f, %.1f) target tag='%s' id='%s'", mousePos.x, mousePos.y,
            realTarget ? realTarget->GetTagName().c_str() : "null", realTarget ? realTarget->GetId().c_str() : "null");

        if (Rml::Element* select = document_->GetElementById("display-mode-select")) {
            const Rml::Vector2f selectOffset = select->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f selectSize = select->GetBox().GetSize(Rml::BoxArea::Border);
            Rml::Log::Message(Rml::Log::LT_INFO, "[OptionsScene] select box origin=(%.1f, %.1f) size=(%.1f, %.1f)", selectOffset.x,
                selectOffset.y, selectSize.x, selectSize.y);
            Rml::Element* selectbox = nullptr;
            for (int i = 0; i < select->GetNumChildren(true); ++i) {
                Rml::Element* child = select->GetChild(i);
                if (child && child->GetTagName() == "selectbox") {
                    selectbox = child;
                    break;
                }
            }
            if (selectbox) {
                const Rml::Vector2f boxOffset = selectbox->GetAbsoluteOffset(Rml::BoxArea::Border);
                const Rml::Vector2f boxSize = selectbox->GetBox().GetSize(Rml::BoxArea::Border);
                Rml::Log::Message(Rml::Log::LT_INFO, "[OptionsScene] selectbox origin=(%.1f, %.1f) size=(%.1f, %.1f) visible=%d", boxOffset.x,
                    boxOffset.y, boxSize.x, boxSize.y, selectbox->IsVisible());
                if (Rml::Element* firstOption = selectbox->GetFirstChild()) {
                    const Rml::Vector2f optOffset = firstOption->GetAbsoluteOffset(Rml::BoxArea::Border);
                    const Rml::Vector2f optSize = firstOption->GetBox().GetSize(Rml::BoxArea::Border);
                    Rml::Log::Message(Rml::Log::LT_INFO, "[OptionsScene] first option origin=(%.1f, %.1f) size=(%.1f, %.1f)", optOffset.x,
                        optOffset.y, optSize.x, optSize.y);
                }
            } else {
                Rml::Log::Message(Rml::Log::LT_INFO, "[OptionsScene] selectbox child not found");
            }
        }
    }

    if (event == Rml::EventId::Click) {
        if (id == "apply-button") {
            if (Rml::Context* context = target->GetContext()) {
                applyDisplaySettingsLive(*context);
            }
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
    } else if (id == "exclusive-fullscreen-checkbox") {
        settings_.exclusiveFullscreen = event.GetParameter<bool>("checked", false);
    } else if (id == "vsync-checkbox") {
        settings_.vSyncEnabled = event.GetParameter<bool>("checked", false);
    } else if (id == "display-mode-select") {
        DisplayModeOption mode{};
        if (parseDisplayModeValue(event.GetParameter<Rml::String>("value", ""), mode)) {
            settings_.displayWidth = mode.width;
            settings_.displayHeight = mode.height;
            settings_.refreshRate = mode.refreshRate;
        }
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

void OptionsScene::applyDisplaySettingsLive(Rml::Context& context) {
    Backend::ApplyDisplaySettings(context, settings_.fullscreen, settings_.exclusiveFullscreen, settings_.displayWidth, settings_.displayHeight,
        settings_.refreshRate);
    Backend::SetVSyncEnabled(settings_.vSyncEnabled);
}

} // namespace NodeSpireUi

