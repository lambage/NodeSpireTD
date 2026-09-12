#pragma once

#include "AppSettings.hpp"
#include "SettingsManager.hpp"
#include "rmlui/IScene.hpp"

#include <RmlUi/Core/EventListener.h>

#include <optional>
#include <string>
#include <vector>

namespace Rml {
class ElementDocument;
class Context;
}

class AudioEngine;

namespace NodeSpireUi {

// Options/settings screen reached from MainMenu's Options button. Implements
// the GFX/Audio/Gameplay tabs using RmlUi form controls (checkbox/range/select
// inputs), backed by SettingsManager and AppSettings. On Apply, GFX settings (fullscreen,
// display mode, vsync) are applied to the live SDL/Vulkan backend in addition to
// being persisted; audio volume sliders apply live to AudioEngine as they change
// (in addition to being persisted on Apply).
class OptionsScene final : public IScene, public Rml::EventListener {
  public:
    void onEnter(Rml::Context& context, AudioEngine& audio) override;
    void onExit(Rml::Context& context) override;
    SceneTransition update(float dt) override;

    void ProcessEvent(Rml::Event& event) override;

    struct DisplayModeOption {
      int width;
      int height;
      int refreshRate;
    };
    
  private:

    void populateControlsFromSettings();
    void populateDisplayModeOptions();
    void populateAudioDeviceOptions();
    void addListeners();
    void removeListeners();
    void setValueLabel(const std::string& labelId, const std::string& text);
    void applyDisplaySettingsLive(Rml::Context& context);

    Rml::ElementDocument* document_ = nullptr;
    AudioEngine* audio_ = nullptr;
    SceneTransition pendingTransition_;
    SettingsManager settingsManager_;
    AppSettings settings_;
    std::vector<DisplayModeOption> displayModes_;
};

} // namespace NodeSpireUi
