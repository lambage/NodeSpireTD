#pragma once

#include "lambui/IScene.hpp"

namespace LambUI {
class UIManager;
}

namespace NodeSpireUi {

// TODO(lambui-migration): compile-only placeholder. The RmlUi-era
// OptionsScene (GFX/Audio/Gameplay tabs backed by SettingsManager/
// AppSettings) has not been rebuilt against LambUI/Lua yet; see
// docs/adr or repo memory for the migration's phased plan.
class OptionsScene final : public IScene {
  public:
    void onEnter(LambUI::UIManager& ui, AudioEngine& audio) override;
    void onExit(LambUI::UIManager& ui) override;
    SceneTransition update(float dt) override;
};

} // namespace NodeSpireUi

