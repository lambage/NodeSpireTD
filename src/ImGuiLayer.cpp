#include "ImGuiLayer.hpp"

#include "VulkanContext.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <imgui_impl_vulkan.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

ImGuiKey ImGuiLayer::translateSfmlKeyToImGui(sf::Keyboard::Key key) {
    switch (key) {
    case sf::Keyboard::Key::A:
        return ImGuiKey_A;
    case sf::Keyboard::Key::B:
        return ImGuiKey_B;
    case sf::Keyboard::Key::C:
        return ImGuiKey_C;
    case sf::Keyboard::Key::D:
        return ImGuiKey_D;
    case sf::Keyboard::Key::E:
        return ImGuiKey_E;
    case sf::Keyboard::Key::F:
        return ImGuiKey_F;
    case sf::Keyboard::Key::G:
        return ImGuiKey_G;
    case sf::Keyboard::Key::H:
        return ImGuiKey_H;
    case sf::Keyboard::Key::I:
        return ImGuiKey_I;
    case sf::Keyboard::Key::J:
        return ImGuiKey_J;
    case sf::Keyboard::Key::K:
        return ImGuiKey_K;
    case sf::Keyboard::Key::L:
        return ImGuiKey_L;
    case sf::Keyboard::Key::M:
        return ImGuiKey_M;
    case sf::Keyboard::Key::N:
        return ImGuiKey_N;
    case sf::Keyboard::Key::O:
        return ImGuiKey_O;
    case sf::Keyboard::Key::P:
        return ImGuiKey_P;
    case sf::Keyboard::Key::Q:
        return ImGuiKey_Q;
    case sf::Keyboard::Key::R:
        return ImGuiKey_R;
    case sf::Keyboard::Key::S:
        return ImGuiKey_S;
    case sf::Keyboard::Key::T:
        return ImGuiKey_T;
    case sf::Keyboard::Key::U:
        return ImGuiKey_U;
    case sf::Keyboard::Key::V:
        return ImGuiKey_V;
    case sf::Keyboard::Key::W:
        return ImGuiKey_W;
    case sf::Keyboard::Key::X:
        return ImGuiKey_X;
    case sf::Keyboard::Key::Y:
        return ImGuiKey_Y;
    case sf::Keyboard::Key::Z:
        return ImGuiKey_Z;
    case sf::Keyboard::Key::Num0:
        return ImGuiKey_0;
    case sf::Keyboard::Key::Num1:
        return ImGuiKey_1;
    case sf::Keyboard::Key::Num2:
        return ImGuiKey_2;
    case sf::Keyboard::Key::Num3:
        return ImGuiKey_3;
    case sf::Keyboard::Key::Num4:
        return ImGuiKey_4;
    case sf::Keyboard::Key::Num5:
        return ImGuiKey_5;
    case sf::Keyboard::Key::Num6:
        return ImGuiKey_6;
    case sf::Keyboard::Key::Num7:
        return ImGuiKey_7;
    case sf::Keyboard::Key::Num8:
        return ImGuiKey_8;
    case sf::Keyboard::Key::Num9:
        return ImGuiKey_9;
    case sf::Keyboard::Key::Escape:
        return ImGuiKey_Escape;
    case sf::Keyboard::Key::LControl:
        return ImGuiKey_LeftCtrl;
    case sf::Keyboard::Key::LShift:
        return ImGuiKey_LeftShift;
    case sf::Keyboard::Key::LAlt:
        return ImGuiKey_LeftAlt;
    case sf::Keyboard::Key::RControl:
        return ImGuiKey_RightCtrl;
    case sf::Keyboard::Key::RShift:
        return ImGuiKey_RightShift;
    case sf::Keyboard::Key::RAlt:
        return ImGuiKey_RightAlt;
    case sf::Keyboard::Key::Space:
        return ImGuiKey_Space;
    case sf::Keyboard::Key::Enter:
        return ImGuiKey_Enter;
    case sf::Keyboard::Key::Backspace:
        return ImGuiKey_Backspace;
    case sf::Keyboard::Key::Tab:
        return ImGuiKey_Tab;
    case sf::Keyboard::Key::Left:
        return ImGuiKey_LeftArrow;
    case sf::Keyboard::Key::Right:
        return ImGuiKey_RightArrow;
    case sf::Keyboard::Key::Up:
        return ImGuiKey_UpArrow;
    case sf::Keyboard::Key::Down:
        return ImGuiKey_DownArrow;
    default:
        return ImGuiKey_None;
    }
}

ImGuiLayer::ImGuiLayer() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    applyModernStyle();
    loadUiFonts();
}

ImGuiLayer::~ImGuiLayer() {
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiLayer::initializeVulkanBackend(const VulkanContext& context) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(context.extent().width), static_cast<float>(context.extent().height));

    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkPipelineRenderingCreateInfo renderingCreateInfo{};
    renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &colorFormat;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = context.instance();
    initInfo.PhysicalDevice = context.physicalDevice();
    initInfo.Device = context.device();
    initInfo.QueueFamily = context.graphicsQueueFamily();
    initInfo.Queue = context.graphicsQueue();
    initInfo.DescriptorPool = context.descriptorPool();

    const uint32_t imageCount = std::max(2u, context.swapchainImageCount());
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;

    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = renderingCreateInfo;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend.");
    }

    splashTexture_.emplace(context.device(), context.allocator(), context.commandPool(), context.graphicsQueue());
    if (!splashTexture_->loadFromFile("assets/images/splash_screen.png")) {
        spdlog::warn("Splash texture could not be loaded. Falling back to text-only splash screen.");
        splashTexture_.reset();
    }
}

void ImGuiLayer::processEvent(const sf::Event& event) {
    ImGuiIO& io = ImGui::GetIO();

    if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        io.DisplaySize = ImVec2(static_cast<float>(resized->size.x), static_cast<float>(resized->size.y));
    } else if (const auto* mouseMoved = event.getIf<sf::Event::MouseMoved>()) {
        io.MousePos = ImVec2(static_cast<float>(mouseMoved->position.x), static_cast<float>(mouseMoved->position.y));
    } else if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mousePressed->button == sf::Mouse::Button::Left) {
            io.MouseDown[0] = true;
        }
        if (mousePressed->button == sf::Mouse::Button::Right) {
            io.MouseDown[1] = true;
        }
    } else if (const auto* mouseReleased = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (mouseReleased->button == sf::Mouse::Button::Left) {
            io.MouseDown[0] = false;
        }
        if (mouseReleased->button == sf::Mouse::Button::Right) {
            io.MouseDown[1] = false;
        }
    } else if (const auto* mouseWheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
        io.MouseWheel += mouseWheel->delta;
    } else if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
        ImGuiKey imKey = translateSfmlKeyToImGui(keyPressed->code);
        if (imKey != ImGuiKey_None) {
            io.AddKeyEvent(imKey, true);
        }

        io.AddKeyEvent(ImGuiMod_Ctrl, keyPressed->control);
        io.AddKeyEvent(ImGuiMod_Shift, keyPressed->shift);
        io.AddKeyEvent(ImGuiMod_Alt, keyPressed->alt);
        io.AddKeyEvent(ImGuiMod_Super, keyPressed->system);
    } else if (const auto* keyReleased = event.getIf<sf::Event::KeyReleased>()) {
        ImGuiKey imKey = translateSfmlKeyToImGui(keyReleased->code);
        if (imKey != ImGuiKey_None) {
            io.AddKeyEvent(imKey, false);
        }

        io.AddKeyEvent(ImGuiMod_Ctrl, keyReleased->control);
        io.AddKeyEvent(ImGuiMod_Shift, keyReleased->shift);
        io.AddKeyEvent(ImGuiMod_Alt, keyReleased->alt);
        io.AddKeyEvent(ImGuiMod_Super, keyReleased->system);
    } else if (const auto* textEntered = event.getIf<sf::Event::TextEntered>()) {
        if (textEntered->unicode >= 32 && textEntered->unicode < 127) {
            io.AddInputCharacter(textEntered->unicode);
        }
    }
}

void ImGuiLayer::setDeltaTime(float deltaSeconds) {
    ImGui::GetIO().DeltaTime = deltaSeconds;
}

void ImGuiLayer::setDisplaySize(uint32_t width, uint32_t height) {
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
}

void ImGuiLayer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() {
    ImGui::Render();
}

void ImGuiLayer::renderDrawData(VkCommandBuffer commandBuffer) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

bool ImGuiLayer::hasSplashTexture() const {
    return splashTexture_.has_value() && splashTexture_->isValid();
}

uint32_t ImGuiLayer::splashTextureWidth() const {
    return hasSplashTexture() ? splashTexture_->width() : 0;
}

uint32_t ImGuiLayer::splashTextureHeight() const {
    return hasSplashTexture() ? splashTexture_->height() : 0;
}

ImTextureRef ImGuiLayer::splashTextureRef() const {
    return hasSplashTexture() ? splashTexture_->textureRef() : ImTextureRef{};
}

void ImGuiLayer::applyModernStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 12.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.ScrollbarRounding = 8.0f;
    style.TabRounding = 8.0f;

    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;

    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.97f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.58f, 0.63f, 1.0f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.10f, 0.13f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.14f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.24f, 0.30f, 0.70f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.19f, 0.24f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.22f, 0.28f, 1.0f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.10f, 0.12f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.12f, 1.0f);

    colors[ImGuiCol_Button] = ImVec4(0.13f, 0.44f, 0.57f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.52f, 0.67f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.10f, 0.37f, 0.48f, 1.0f);

    colors[ImGuiCol_Header] = ImVec4(0.13f, 0.44f, 0.57f, 0.55f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.52f, 0.67f, 0.70f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.10f, 0.37f, 0.48f, 0.90f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.43f, 0.80f, 0.90f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.76f, 0.86f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.29f, 0.64f, 0.74f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.28f, 0.33f, 0.90f);
}

void ImGuiLayer::loadUiFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const std::array<const char*, 4> regularCandidates = {"assets/fonts/Inter-Regular.ttf", "assets/fonts/SegoeUI.ttf",
                                                          "assets/fonts/Roboto-Regular.ttf", "assets/fonts/times.ttf"};
    const std::array<const char*, 3> headingCandidates = {
        "assets/fonts/Inter-Bold.ttf", "assets/fonts/Inter-SemiBold.ttf", "assets/fonts/timesbd.ttf"};

    ImFont* regularFont = nullptr;
    for (const char* candidate : regularCandidates) {
        if (std::filesystem::exists(candidate)) {
            regularFont = io.Fonts->AddFontFromFileTTF(candidate, 18.0f);
            if (regularFont != nullptr) {
                break;
            }
        }
    }

    for (const char* candidate : headingCandidates) {
        if (std::filesystem::exists(candidate)) {
            headingFont_ = io.Fonts->AddFontFromFileTTF(candidate, 26.0f);
            if (headingFont_ != nullptr) {
                break;
            }
        }
    }

    for (const char* candidate : headingCandidates) {
        if (std::filesystem::exists(candidate)) {
            titleFont_ = io.Fonts->AddFontFromFileTTF(candidate, 52.0f);
            if (titleFont_ != nullptr) {
                break;
            }
        }
    }

    if (regularFont == nullptr) {
        regularFont = io.Fonts->AddFontDefault();
    }
    if (headingFont_ == nullptr) {
        headingFont_ = regularFont;
    }

    io.FontDefault = regularFont;
}
