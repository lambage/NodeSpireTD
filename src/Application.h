#pragma once

#include <memory>
#include <string>
#include <string_view>

// Forward-declare GLFW to avoid including the full header in consumers.
struct GLFWwindow;

namespace NST {

class VulkanContext;
class ImGuiLayer;
class UIManager;

// ---------------------------------------------------------------
// Application
//
// Top-level owner of the GLFW window, Vulkan context, ImGui layer,
// and UI state machine.  The main() function constructs one
// Application and calls run().
// ---------------------------------------------------------------
class Application {
public:
    Application(std::string_view title, int width, int height);
    ~Application();

    // Non-copyable / non-movable: owns OS + GPU resources.
    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&)                 = delete;
    Application& operator=(Application&&)      = delete;

    /// Enters the main loop.  Returns when the user closes the window or
    /// the UI state transitions to MenuState::Exit.
    void run();

    // Accessors used by subsystems (UIManager, systems, …).
    [[nodiscard]] GLFWwindow*    window()        const noexcept;
    [[nodiscard]] VulkanContext& vulkanContext()  noexcept;
    [[nodiscard]] UIManager&     uiManager()     noexcept;

private:
    bool init();
    void mainLoop();
    void shutdown();

    std::string  m_title;
    int          m_width;
    int          m_height;
    GLFWwindow*  m_window{nullptr};

    std::unique_ptr<VulkanContext> m_vulkan;
    std::unique_ptr<ImGuiLayer>    m_imgui;
    std::unique_ptr<UIManager>     m_ui;
};

} // namespace NST
