#pragma once

// volk must be included before imgui_impl_vulkan.h.
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <volk.h>


namespace NST {

class VulkanContext;

// ---------------------------------------------------------------
// ImGuiLayer
//
// Wraps Dear ImGui initialisation for the GLFW + Vulkan backend.
// Owns no Vulkan objects – they all live in VulkanContext.
//
// Usage per-frame:
//   layer.beginFrame();
//   ImGui::…
//   layer.endFrame(commandBuffer);  // records ImGui draw calls
// ---------------------------------------------------------------
class ImGuiLayer {
  public:
    bool init(GLFWwindow* window, VulkanContext& ctx);
    void destroy();

    void beginFrame();
    void endFrame(VkCommandBuffer cmd);

  private:
    bool m_initialised{false};
};

} // namespace NST
