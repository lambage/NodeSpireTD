#include "renderer/ImGuiLayer.h"
#include "renderer/VulkanContext.h"

#include <cstdio>

namespace NST {

// ---------------------------------------------------------------
bool ImGuiLayer::init(GLFWwindow* window, VulkanContext& ctx) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Dark slate theme matching the engine aesthetic.
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 6.0f;
    style.FrameRounding    = 4.0f;
    style.GrabRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.ItemSpacing      = {10.0f, 8.0f};
    style.FramePadding     = {8.0f, 4.0f};

    // Override key colours to match the dark-slate design spec.
    auto& c = style.Colors;
    c[ImGuiCol_WindowBg]         = {0.10f, 0.12f, 0.15f, 1.00f};
    c[ImGuiCol_TitleBg]          = {0.08f, 0.10f, 0.12f, 1.00f};
    c[ImGuiCol_TitleBgActive]    = {0.08f, 0.10f, 0.12f, 1.00f};
    c[ImGuiCol_Header]           = {0.20f, 0.25f, 0.32f, 1.00f};
    c[ImGuiCol_HeaderHovered]    = {0.26f, 0.33f, 0.42f, 1.00f};
    c[ImGuiCol_Button]           = {0.20f, 0.25f, 0.32f, 1.00f};
    c[ImGuiCol_ButtonHovered]    = {0.30f, 0.38f, 0.48f, 1.00f};
    c[ImGuiCol_ButtonActive]     = {0.16f, 0.20f, 0.26f, 1.00f};
    c[ImGuiCol_FrameBg]          = {0.14f, 0.17f, 0.22f, 1.00f};
    c[ImGuiCol_FrameBgHovered]   = {0.20f, 0.25f, 0.32f, 1.00f};
    c[ImGuiCol_CheckMark]        = {0.40f, 0.72f, 1.00f, 1.00f};
    c[ImGuiCol_SliderGrab]       = {0.40f, 0.72f, 1.00f, 1.00f};
    c[ImGuiCol_SliderGrabActive] = {0.55f, 0.82f, 1.00f, 1.00f};

    // GLFW backend.
    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
        std::fprintf(stderr, "[ImGuiLayer] GLFW backend init failed.\n");
        return false;
    }

    // Vulkan backend – supply a function loader so that ImGui resolves
    // Vulkan symbols through Volk instead of requiring linked prototypes.
    // Store the instance in a local so we can safely take its address.
    VkInstance vkInst = ctx.instance();
    ImGui_ImplVulkan_LoadFunctions(
        [](const char* func, void* userdata) -> PFN_vkVoidFunction {
            return vkGetInstanceProcAddr(
                *reinterpret_cast<VkInstance*>(userdata), func);
        },
        &vkInst);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance        = ctx.instance();
    initInfo.PhysicalDevice  = ctx.physicalDevice();
    initInfo.Device          = ctx.device();
    initInfo.QueueFamily     = ctx.graphicsFamily();
    initInfo.Queue           = ctx.graphicsQueue();
    initInfo.DescriptorPool  = ctx.imguiPool();
    initInfo.RenderPass      = ctx.renderPass();
    initInfo.MinImageCount   = 2;
    initInfo.ImageCount      = ctx.swapchainImageCount();
    initInfo.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        std::fprintf(stderr, "[ImGuiLayer] Vulkan backend init failed.\n");
        return false;
    }

    // Upload default fonts.
    ImGui_ImplVulkan_CreateFontsTexture();

    m_initialised = true;
    return true;
}

// ---------------------------------------------------------------
void ImGuiLayer::destroy() {
    if (!m_initialised) return;
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_initialised = false;
}

// ---------------------------------------------------------------
void ImGuiLayer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

// ---------------------------------------------------------------
void ImGuiLayer::endFrame(VkCommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

} // namespace NST
