#include "Application.h"

#include <cstdio>
#include <stdexcept>

// volk must be included before any other Vulkan header.
#include <volk.h>

#include <GLFW/glfw3.h>

#include "renderer/VulkanContext.h"
#include "renderer/ImGuiLayer.h"
#include "ui/UIManager.h"

namespace NST {

// ---------------------------------------------------------------
Application::Application(std::string_view title, int width, int height)
    : m_title(title), m_width(width), m_height(height)
{
    if (!init())
        throw std::runtime_error("[Application] Initialisation failed.");
}

Application::~Application() {
    shutdown();
}

// ---------------------------------------------------------------
bool Application::init() {
    // --- Volk: load the Vulkan loader library ---
    if (volkInitialize() != VK_SUCCESS) {
        std::fprintf(stderr, "[Application] volkInitialize failed. "
                     "Is the Vulkan loader installed?\n");
        return false;
    }

    // --- GLFW ---
    if (!glfwInit()) {
        std::fprintf(stderr, "[Application] glfwInit failed.\n");
        return false;
    }

    // No OpenGL context – pure Vulkan.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(),
                                nullptr, nullptr);
    if (!m_window) {
        std::fprintf(stderr, "[Application] glfwCreateWindow failed.\n");
        glfwTerminate();
        return false;
    }

    // --- Vulkan ---
    m_vulkan = std::make_unique<VulkanContext>();
    if (!m_vulkan->init(m_window)) return false;

    // --- ImGui ---
    m_imgui = std::make_unique<ImGuiLayer>();
    if (!m_imgui->init(m_window, *m_vulkan)) return false;

    // --- UI state machine ---
    m_ui = std::make_unique<UIManager>(*this);

    return true;
}

// ---------------------------------------------------------------
void Application::run() {
    mainLoop();
}

// ---------------------------------------------------------------
void Application::mainLoop() {
    while (!glfwWindowShouldClose(m_window) &&
           m_ui->state() != MenuState::Exit)
    {
        glfwPollEvents();

        // Handle minimised window.
        int w = 0, h = 0;
        glfwGetFramebufferSize(m_window, &w, &h);
        while (w == 0 || h == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(m_window, &w, &h);
        }

        m_imgui->beginFrame();
        m_ui->render(w, h);

        auto [cmd, fb, imageIndex, valid] = m_vulkan->beginFrame();
        if (!valid) continue;

        // Begin render pass.
        VkClearValue clearColor{{{0.08f, 0.10f, 0.12f, 1.0f}}};
        VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpBegin.renderPass        = m_vulkan->renderPass();
        rpBegin.framebuffer       = fb;
        rpBegin.renderArea.extent = m_vulkan->swapchainExtent();
        rpBegin.clearValueCount   = 1;
        rpBegin.pClearValues      = &clearColor;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        m_imgui->endFrame(cmd);  // records ImGui draw data
        vkCmdEndRenderPass(cmd);

        m_vulkan->endFrame(cmd, imageIndex);
    }
}

// ---------------------------------------------------------------
void Application::shutdown() {
    if (m_vulkan && m_vulkan->device())
        vkDeviceWaitIdle(m_vulkan->device());

    m_imgui.reset();
    m_vulkan.reset();

    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}

// ---------------------------------------------------------------
GLFWwindow* Application::window() const noexcept { return m_window; }
VulkanContext& Application::vulkanContext() noexcept { return *m_vulkan; }
UIManager& Application::uiManager() noexcept { return *m_ui; }

} // namespace NST
