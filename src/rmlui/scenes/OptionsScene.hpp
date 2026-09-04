#pragma once

#include "AppSettings.hpp"
#include "SettingsManager.hpp"
#include "rmlui/IScene.hpp"

#include <RmlUi/Core/EventListener.h>

#include <optional>
#include <string>

namespace Rml {
class ElementDocument;
}

namespace NodeSpireUi {

// Options/settings screen reached from MainMenu's Options button. Recreates
// the legacy ImGui Options screen's GFX/Audio/Gameplay tabs using RmlUi form
// controls (checkbox/range inputs), backed by the same SettingsManager and
// AppSettings used by NodeSpireTD-imgui. Video/audio settings are persisted
// on Apply but not yet applied live (windowing/audio migration are separate
// phases), matching the "settings take effect next launch" fallback.
class OptionsScene final : public IScene, public Rml::EventListener {
  public:
    void onEnter(Rml::Context& context) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;

    void ProcessEvent(Rml::Event& event) override;

  private:
    void populateControlsFromSettings();
    void addListeners();
    void removeListeners();
    void setValueLabel(const std::string& labelId, const std::string& text);

    Rml::ElementDocument* document_ = nullptr;
    SceneTransition pendingTransition_;
    SettingsManager settingsManager_;
    AppSettings settings_;
};

} // namespace NodeSpireUi
