#pragma once

#include "lambui/IScene.hpp"

class VulkanContext;

namespace multiplayer {
class MultiplayerSession;
}

namespace NodeSpireUi {

// TODO(lambui-migration): compile-only placeholder. The RmlUi-era
// PlayLevelScene (world rendering, tower placement, HUD, and the
// host-authoritative match simulation) has not been rebuilt against
// LambUI/Lua yet -- this is by far the largest remaining migration slice.
// WorldRenderer/TowerLoadController/EnemyLoadController/MatchSimulation are
// deliberately not wired up here yet; see the removed RmlUi implementation
// (git history) for the logic to port over.
class PlayLevelScene final : public IScene {
  public:
    PlayLevelScene(VulkanContext& vulkanContext, multiplayer::MultiplayerSession& session,
                   const PlayLevelLaunchConfig& launchConfig);
    ~PlayLevelScene() override;

    void onEnter(LambUI::UIManager& ui, AudioEngine& audio) override;
    void onExit(LambUI::UIManager& ui) override;
    SceneTransition update(float dt) override;

  private:
    VulkanContext& vulkanContext_;
    multiplayer::MultiplayerSession& session_;
    PlayLevelLaunchConfig launchConfig_;
};

} // namespace NodeSpireUi

