#include "ui/UIManager.h"

#include "Application.h"

#include "renderer/VulkanContext.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imnodes.h>
#include <stb_image.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>


namespace NST {

UIManager::UIManager(Application& app) : m_app(app), m_splashStart(std::chrono::steady_clock::now()) {
    m_splashTextureLoaded = loadSplashTexture();
}

UIManager::~UIManager() {
    destroySplashTexture();
}

// ---------------------------------------------------------------
void UIManager::render(int width, int height) {
    switch (m_state) {
    case MenuState::Splash:
        renderSplash(width, height);
        break;
    case MenuState::MainMenu:
        renderMainMenu(width, height);
        break;
    case MenuState::Gameplay:
        renderGameplay(width, height);
        break;
    case MenuState::Settings:
        renderSettings(width, height);
        break;
    case MenuState::Exit:
        break; // handled by Application
    }
}

// ---------------------------------------------------------------
void UIManager::renderSplash(int width, int height) {
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);

    ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({fw, fh}, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.04f, 1.00f));

    if (ImGui::Begin("##Splash", nullptr, flags)) {
        if (m_splashTextureLoaded && m_splashDescriptorSet) {
            const float imageAspect = static_cast<float>(m_splashWidth) / static_cast<float>(m_splashHeight);
            float drawW = fw;
            float drawH = drawW / imageAspect;

            if (drawH > fh) {
                drawH = fh;
                drawW = drawH * imageAspect;
            }

            const float posX = (fw - drawW) * 0.5f;
            const float posY = (fh - drawH) * 0.5f;
            ImGui::SetCursorPos({posX, posY});
            ImGui::Image((ImTextureID)(intptr_t)m_splashDescriptorSet, {drawW, drawH});
        } else {
            ImGui::SetCursorPos({fw * 0.5f - 140.0f, fh * 0.5f - 16.0f});
            ImGui::TextUnformatted("Loading NodeSpire TD...");
        }

        const auto elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_splashStart).count();
        if (elapsed >= 1.75f) {
            m_state = MenuState::MainMenu;
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------
bool UIManager::loadSplashTexture() {
    namespace fs = std::filesystem;

    std::vector<fs::path> candidates;
    const fs::path imageDir = fs::path("assets") / "images";
    if (!fs::exists(imageDir) || !fs::is_directory(imageDir)) {
        return false;
    }

    std::array<std::string, 5> supported{ ".png", ".jpg", ".jpeg", ".bmp", ".tga" };

    for (const auto& entry : fs::directory_iterator(imageDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (std::find(supported.begin(), supported.end(), ext) != supported.end()) {
            candidates.push_back(entry.path());
        }
    }

    if (candidates.empty()) {
        return false;
    }

    std::sort(candidates.begin(), candidates.end());

    fs::path imagePath = candidates.front();
    for (const auto& candidate : candidates) {
        std::string stem = candidate.stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (stem.find("splash") != std::string::npos) {
            imagePath = candidate;
            break;
        }
    }

    int texW = 0;
    int texH = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(imagePath.string().c_str(), &texW, &texH, &channels, STBI_rgb_alpha);
    if (!pixels || texW <= 0 || texH <= 0) {
        return false;
    }

    const VkDeviceSize uploadSize = static_cast<VkDeviceSize>(texW) * static_cast<VkDeviceSize>(texH) * 4;
    VulkanContext& vk = m_app.vulkanContext();

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo stagingInfo{};

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = uploadSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (vmaCreateBuffer(vk.allocator(), &bufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, &stagingInfo) !=
        VK_SUCCESS) {
        stbi_image_free(pixels);
        return false;
    }

    std::memcpy(stagingInfo.pMappedData, pixels, static_cast<size_t>(uploadSize));
    stbi_image_free(pixels);

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = {static_cast<uint32_t>(texW), static_cast<uint32_t>(texH), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imageAllocInfo{};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    if (vmaCreateImage(vk.allocator(), &imageInfo, &imageAllocInfo, &m_splashImage, &m_splashAllocation, nullptr) !=
        VK_SUCCESS) {
        vmaDestroyBuffer(vk.allocator(), stagingBuffer, stagingAlloc);
        return false;
    }

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = vk.graphicsFamily();

    if (vkCreateCommandPool(vk.device(), &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        vmaDestroyImage(vk.allocator(), m_splashImage, m_splashAllocation);
        m_splashImage = VK_NULL_HANDLE;
        m_splashAllocation = VK_NULL_HANDLE;
        vmaDestroyBuffer(vk.allocator(), stagingBuffer, stagingAlloc);
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(vk.device(), &cmdAllocInfo, &commandBuffer) != VK_SUCCESS) {
        vkDestroyCommandPool(vk.device(), commandPool, nullptr);
        vmaDestroyImage(vk.allocator(), m_splashImage, m_splashAllocation);
        m_splashImage = VK_NULL_HANDLE;
        m_splashAllocation = VK_NULL_HANDLE;
        vmaDestroyBuffer(vk.allocator(), stagingBuffer, stagingAlloc);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = m_splashImage;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel = 0;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.baseArrayLayer = 0;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = {static_cast<uint32_t>(texW), static_cast<uint32_t>(texH), 1};

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_splashImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &copyRegion);

    VkImageMemoryBarrier toReadable{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toReadable.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadable.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadable.image = m_splashImage;
    toReadable.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toReadable.subresourceRange.baseMipLevel = 0;
    toReadable.subresourceRange.levelCount = 1;
    toReadable.subresourceRange.baseArrayLayer = 0;
    toReadable.subresourceRange.layerCount = 1;
    toReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toReadable);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(vk.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vk.graphicsQueue());

    vkDestroyCommandPool(vk.device(), commandPool, nullptr);
    vmaDestroyBuffer(vk.allocator(), stagingBuffer, stagingAlloc);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_splashImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(vk.device(), &viewInfo, nullptr, &m_splashImageView) != VK_SUCCESS) {
        vmaDestroyImage(vk.allocator(), m_splashImage, m_splashAllocation);
        m_splashImage = VK_NULL_HANDLE;
        m_splashAllocation = VK_NULL_HANDLE;
        return false;
    }

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxAnisotropy = 1.0f;

    if (vkCreateSampler(vk.device(), &samplerInfo, nullptr, &m_splashSampler) != VK_SUCCESS) {
        vkDestroyImageView(vk.device(), m_splashImageView, nullptr);
        m_splashImageView = VK_NULL_HANDLE;
        vmaDestroyImage(vk.allocator(), m_splashImage, m_splashAllocation);
        m_splashImage = VK_NULL_HANDLE;
        m_splashAllocation = VK_NULL_HANDLE;
        return false;
    }

    m_splashDescriptorSet = ImGui_ImplVulkan_AddTexture(m_splashSampler, m_splashImageView,
                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!m_splashDescriptorSet) {
        vkDestroySampler(vk.device(), m_splashSampler, nullptr);
        m_splashSampler = VK_NULL_HANDLE;
        vkDestroyImageView(vk.device(), m_splashImageView, nullptr);
        m_splashImageView = VK_NULL_HANDLE;
        vmaDestroyImage(vk.allocator(), m_splashImage, m_splashAllocation);
        m_splashImage = VK_NULL_HANDLE;
        m_splashAllocation = VK_NULL_HANDLE;
        return false;
    }

    m_splashWidth = texW;
    m_splashHeight = texH;
    return true;
}

// ---------------------------------------------------------------
void UIManager::destroySplashTexture() {
    VulkanContext& vk = m_app.vulkanContext();

    if (m_splashDescriptorSet) {
        ImGui_ImplVulkan_RemoveTexture(m_splashDescriptorSet);
        m_splashDescriptorSet = VK_NULL_HANDLE;
    }
    if (m_splashSampler) {
        vkDestroySampler(vk.device(), m_splashSampler, nullptr);
        m_splashSampler = VK_NULL_HANDLE;
    }
    if (m_splashImageView) {
        vkDestroyImageView(vk.device(), m_splashImageView, nullptr);
        m_splashImageView = VK_NULL_HANDLE;
    }
    if (m_splashImage && m_splashAllocation) {
        vmaDestroyImage(vk.allocator(), m_splashImage, m_splashAllocation);
        m_splashImage = VK_NULL_HANDLE;
        m_splashAllocation = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------
// renderMainMenu
//
// Dark-slate, left-aligned asymmetric panel occupying roughly the
// left third of the screen.  Clean, responsive layout.
// ---------------------------------------------------------------
void UIManager::renderMainMenu(int width, int height) {
    const float panelW = static_cast<float>(width) * 0.32f;
    const float panelH = static_cast<float>(height) * 0.72f;
    const float panelX = static_cast<float>(width) * 0.06f;
    const float panelY = (static_cast<float>(height) - panelH) * 0.5f;

    ImGui::SetNextWindowPos({panelX, panelY}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({panelW, panelH}, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {24.0f, 24.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 16.0f});

    if (ImGui::Begin("##MainMenu", nullptr, flags)) {
        // --- Title ---
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.72f, 1.00f, 1.00f));
        ImGui::SetWindowFontScale(1.9f);
        ImGui::TextUnformatted("NODESPIRE TD");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.65f, 0.70f, 1.00f));
        ImGui::TextUnformatted("Tower Defense · Talent Mastery");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Spacing();

        // --- Buttons ---
        const float btnW = panelW - 48.0f;
        const float btnH = 44.0f;

        if (ImGui::Button("Play", {btnW, btnH})) {
            m_state = MenuState::Gameplay;
        }

        if (ImGui::Button("Settings", {btnW, btnH})) {
            m_state = MenuState::Settings;
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.18f, 0.18f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.24f, 0.24f, 1.00f));
        if (ImGui::Button("Exit", {btnW, btnH})) {
            m_state = MenuState::Exit;
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------
// renderGameplay
//
// Displays an ImNodes-backed talent tree editor and a live stat
// inspector for the selected tower.
// ---------------------------------------------------------------
void UIManager::renderGameplay(int width, int height) {
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);

    // --- Talent Tree panel (right side) ---
    ImGui::SetNextWindowPos({fw * 0.55f, 20.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({fw * 0.43f, fh - 40.0f}, ImGuiCond_Always);

    ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("Talent Tree", nullptr, panelFlags)) {
        ImNodes::BeginNodeEditor();

        // Example node: Rapid Fire
        ImNodes::BeginNode(1);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("Rapid Fire");
        ImNodes::EndNodeTitleBar();
        ImNodes::BeginOutputAttribute(10);
        ImGui::Text("ATK SPD +0.1");
        ImNodes::EndOutputAttribute();
        ImNodes::EndNode();

        // Example node: Eagle Eye
        ImNodes::BeginNode(2);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("Eagle Eye");
        ImNodes::EndNodeTitleBar();
        ImNodes::BeginInputAttribute(20);
        ImGui::Text("Range +1.0");
        ImNodes::EndInputAttribute();
        ImNodes::EndNode();

        // Example edge connecting the two nodes
        ImNodes::Link(100, 10, 20);

        ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();
    }
    ImGui::End();

    // --- Tower stats inspector (left side) ---
    ImGui::SetNextWindowPos({20.0f, 20.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({fw * 0.30f, 260.0f}, ImGuiCond_Always);

    if (ImGui::Begin("Tower Stats", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("Selected: Archer Tower #1");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("ATK Speed : %.2f / s", 1.0f);
        ImGui::Text("Damage    : %.1f", 10.0f);
        ImGui::Text("Range     : %.1f", 5.0f);
        ImGui::Text("AoE       : %.1f", 0.0f);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Respec", {120.0f, 32.0f})) { /* TODO */
        }
        ImGui::SameLine();
        if (ImGui::Button("Back to Menu", {140.0f, 32.0f})) {
            m_state = MenuState::MainMenu;
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------
// renderSettings
// ---------------------------------------------------------------
void UIManager::renderSettings(int width, int height) {
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);

    ImGui::SetNextWindowPos({fw * 0.20f, fh * 0.15f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({fw * 0.60f, fh * 0.70f}, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    struct ResolutionOption {
        int width;
        int height;
        int refreshRate;
        std::string label;
    };

    struct MonitorOption {
        GLFWmonitor* monitor;
        std::string label;
    };

    struct RevertState {
        bool pending{false};
        GLFWmonitor* previousMonitor{nullptr};
        int prevX{0};
        int prevY{0};
        int prevW{0};
        int prevH{0};
        int prevRefreshRate{GLFW_DONT_CARE};
        std::chrono::steady_clock::time_point deadline{};
    };

    static GLFWmonitor* cachedMonitor = nullptr;
    static std::vector<MonitorOption> availableMonitors;
    static int selectedMonitor = 0;
    static GLFWmonitor* selectedMonitorPtr = nullptr;
    static std::vector<ResolutionOption> availableResolutions;
    static int selectedResolution = 0;
    static RevertState revertState{};

    GLFWwindow* window = m_app.window();
    GLFWmonitor* currentMonitor = glfwGetWindowMonitor(window);

    availableMonitors.clear();
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    for (int i = 0; i < monitorCount; ++i) {
        GLFWmonitor* monitor = monitors[i];
        const char* monitorName = glfwGetMonitorName(monitor);

        MonitorOption option{};
        option.monitor = monitor;
        option.label = "Display " + std::to_string(i + 1);
        if (monitorName && monitorName[0] != '\0') {
            option.label += " - ";
            option.label += monitorName;
        }
        availableMonitors.push_back(std::move(option));
    }

    GLFWmonitor* activeMonitor = currentMonitor ? currentMonitor : glfwGetPrimaryMonitor();

    if (availableMonitors.empty()) {
        selectedMonitor = 0;
        selectedMonitorPtr = nullptr;
    } else {
        if (!selectedMonitorPtr) {
            selectedMonitorPtr = activeMonitor;
        }

        bool foundSelected = false;
        for (int i = 0; i < static_cast<int>(availableMonitors.size()); ++i) {
            if (availableMonitors[i].monitor == selectedMonitorPtr) {
                selectedMonitor = i;
                foundSelected = true;
                break;
            }
        }
        if (!foundSelected) {
            selectedMonitorPtr = activeMonitor;
            for (int i = 0; i < static_cast<int>(availableMonitors.size()); ++i) {
                if (availableMonitors[i].monitor == selectedMonitorPtr) {
                    selectedMonitor = i;
                    foundSelected = true;
                    break;
                }
            }
        }
        if (!foundSelected) {
            selectedMonitor = 0;
            selectedMonitorPtr = availableMonitors[0].monitor;
        }
    }

    GLFWmonitor* modeSourceMonitor = activeMonitor;
    if (!availableMonitors.empty() && selectedMonitor >= 0 &&
        selectedMonitor < static_cast<int>(availableMonitors.size())) {
        modeSourceMonitor = availableMonitors[selectedMonitor].monitor;
    }

    if (modeSourceMonitor != cachedMonitor || availableResolutions.empty()) {
        cachedMonitor = modeSourceMonitor;
        availableResolutions.clear();

        if (modeSourceMonitor) {
            int modeCount = 0;
            const GLFWvidmode* modes = glfwGetVideoModes(modeSourceMonitor, &modeCount);
            for (int i = 0; i < modeCount; ++i) {
                const GLFWvidmode& mode = modes[i];
                auto existing = std::find_if(availableResolutions.begin(), availableResolutions.end(),
                                             [&](const ResolutionOption& option) {
                                                 return option.width == mode.width &&
                                                        option.height == mode.height &&
                                                        option.refreshRate == mode.refreshRate;
                                             });
                if (existing == availableResolutions.end()) {
                    ResolutionOption option{};
                    option.width = mode.width;
                    option.height = mode.height;
                    option.refreshRate = mode.refreshRate;
                    option.label = std::to_string(mode.width) + " x " + std::to_string(mode.height) +
                                   " @ " + std::to_string(mode.refreshRate) + " Hz";
                    availableResolutions.push_back(std::move(option));
                }
            }
        }

        if (availableResolutions.empty()) {
            int w = 0;
            int h = 0;
            glfwGetWindowSize(window, &w, &h);
            ResolutionOption fallback{};
            fallback.width = w;
            fallback.height = h;
            fallback.refreshRate = GLFW_DONT_CARE;
            fallback.label = std::to_string(w) + " x " + std::to_string(h);
            availableResolutions.push_back(std::move(fallback));
        }

        std::sort(availableResolutions.begin(), availableResolutions.end(), [](const ResolutionOption& a, const ResolutionOption& b) {
            const int64_t areaA = static_cast<int64_t>(a.width) * static_cast<int64_t>(a.height);
            const int64_t areaB = static_cast<int64_t>(b.width) * static_cast<int64_t>(b.height);
            if (areaA != areaB) {
                return areaA < areaB;
            }
            if (a.width != b.width) {
                return a.width < b.width;
            }
            return a.refreshRate < b.refreshRate;
        });

        int currentW = 0;
        int currentH = 0;
        glfwGetWindowSize(window, &currentW, &currentH);
        int currentRefresh = GLFW_DONT_CARE;
        if (activeMonitor) {
            const GLFWvidmode* activeMode = glfwGetVideoMode(activeMonitor);
            if (activeMode) {
                currentRefresh = activeMode->refreshRate;
            }
        }

        selectedResolution = 0;
        for (int i = 0; i < static_cast<int>(availableResolutions.size()); ++i) {
            if (availableResolutions[i].width == currentW && availableResolutions[i].height == currentH &&
                (currentRefresh == GLFW_DONT_CARE || availableResolutions[i].refreshRate == currentRefresh)) {
                selectedResolution = i;
                break;
            }
        }
    }

    if (selectedResolution < 0 || selectedResolution >= static_cast<int>(availableResolutions.size())) {
        selectedResolution = 0;
    }

    auto restorePreviousResolution = [&]() {
        if (!revertState.pending) {
            return;
        }

        if (revertState.previousMonitor) {
            glfwSetWindowMonitor(window, revertState.previousMonitor, 0, 0, revertState.prevW, revertState.prevH,
                                 revertState.prevRefreshRate);
        } else {
            glfwSetWindowMonitor(window, nullptr, revertState.prevX, revertState.prevY, revertState.prevW, revertState.prevH,
                                 GLFW_DONT_CARE);
        }
        revertState.pending = false;
    };

    if (revertState.pending && std::chrono::steady_clock::now() >= revertState.deadline) {
        restorePreviousResolution();
    }

    if (ImGui::Begin("Settings", nullptr, flags)) {
        ImGui::SeparatorText("Display");

        static bool vsync = true;
        ImGui::Checkbox("VSync", &vsync);

        ImGui::Spacing();
        ImGui::TextUnformatted("Monitor");
        ImGui::SetNextItemWidth(320.0f);
        const char* selectedMonitorLabel = (!availableMonitors.empty() &&
                                            selectedMonitor >= 0 &&
                                            selectedMonitor < static_cast<int>(availableMonitors.size()))
                                               ? availableMonitors[selectedMonitor].label.c_str()
                                               : "Primary Display";
        if (ImGui::BeginCombo("##Monitor", selectedMonitorLabel)) {
            for (int i = 0; i < static_cast<int>(availableMonitors.size()); ++i) {
                const bool isSelected = (selectedMonitor == i);
                if (ImGui::Selectable(availableMonitors[i].label.c_str(), isSelected)) {
                    selectedMonitor = i;
                    selectedMonitorPtr = availableMonitors[i].monitor;
                    cachedMonitor = nullptr;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Resolution");
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("##Resolution", availableResolutions[selectedResolution].label.c_str())) {
            for (int i = 0; i < static_cast<int>(availableResolutions.size()); ++i) {
                const bool isSelected = (selectedResolution == i);
                if (ImGui::Selectable(availableResolutions[i].label.c_str(), isSelected)) {
                    selectedResolution = i;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (revertState.pending) {
            ImGui::TextColored(ImVec4(1.00f, 0.82f, 0.40f, 1.00f), "Confirming resolution change...");
        }

        if (ImGui::Button("Apply Resolution", {180.0f, 32.0f}) && !revertState.pending) {
            revertState.previousMonitor = currentMonitor;
            glfwGetWindowPos(window, &revertState.prevX, &revertState.prevY);
            glfwGetWindowSize(window, &revertState.prevW, &revertState.prevH);
            if (currentMonitor) {
                const GLFWvidmode* currentMode = glfwGetVideoMode(currentMonitor);
                revertState.prevRefreshRate = currentMode ? currentMode->refreshRate : GLFW_DONT_CARE;
            } else {
                revertState.prevRefreshRate = GLFW_DONT_CARE;
            }

            const ResolutionOption& selected = availableResolutions[selectedResolution];
            GLFWmonitor* targetMonitor = modeSourceMonitor;
            if (targetMonitor) {
                const int refreshRate = selected.refreshRate > 0 ? selected.refreshRate : GLFW_DONT_CARE;
                glfwSetWindowMonitor(window, targetMonitor, 0, 0, selected.width, selected.height, refreshRate);
            } else {
                glfwSetWindowSize(window, selected.width, selected.height);
            }

            revertState.pending = true;
            revertState.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        }

        if (revertState.pending) {
            ImGui::OpenPopup("Confirm Resolution");
            const float secondsLeft = std::max(0.0f,
                                               std::chrono::duration<float>(revertState.deadline - std::chrono::steady_clock::now())
                                                   .count());

            if (ImGui::BeginPopupModal("Confirm Resolution", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
                ImGui::Text("Keep this resolution?");
                ImGui::Text("Reverting in %d seconds", static_cast<int>(secondsLeft));
                ImGui::Spacing();

                if (ImGui::Button("Keep", {120.0f, 0.0f})) {
                    revertState.pending = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Revert", {120.0f, 0.0f})) {
                    restorePreviousResolution();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Audio");

        static float masterVolume = 0.8f;
        ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Back", {100.0f, 32.0f})) {
            m_state = MenuState::MainMenu;
        }
    }
    ImGui::End();
}

} // namespace NST
