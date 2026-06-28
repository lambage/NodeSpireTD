#pragma once

#include <cstdint>

namespace NST {

// ---------------------------------------------------------------
// MenuState – top-level UI state machine
// ---------------------------------------------------------------
enum class MenuState : uint8_t {
    MainMenu,
    Gameplay,
    Settings,
    Exit,
};

class Application; // forward declaration

// ---------------------------------------------------------------
// UIManager
//
// Immediate-mode state machine that drives all Dear ImGui panels.
// Each tick it inspects m_state and delegates to the appropriate
// render function.  State transitions set m_state directly.
// ---------------------------------------------------------------
class UIManager {
  public:
    explicit UIManager(Application& app);

    /// Called once per frame after ImGuiLayer::beginFrame().
    void render(int width, int height);

    [[nodiscard]] MenuState state() const noexcept {
        return m_state;
    }

  private:
    void renderMainMenu(int width, int height);
    void renderGameplay(int width, int height);
    void renderSettings(int width, int height);

    Application& m_app;
    MenuState m_state{MenuState::MainMenu};
};

} // namespace NST
