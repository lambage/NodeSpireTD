#include "ImGuiLayer.hpp"

#include "VulkanContext.hpp"

#include <imgui_impl_vulkan.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

ImGuiKey ImGuiLayer::translateSfmlKeyToImGui(sf::Keyboard::Key key) {
    switch (key) {
        case sf::Keyboard::Key::A: return ImGuiKey_A;
        case sf::Keyboard::Key::B: return ImGuiKey_B;
        case sf::Keyboard::Key::C: return ImGuiKey_C;
        case sf::Keyboard::Key::D: return ImGuiKey_D;
        case sf::Keyboard::Key::E: return ImGuiKey_E;
        case sf::Keyboard::Key::F: return ImGuiKey_F;
        case sf::Keyboard::Key::G: return ImGuiKey_G;
        case sf::Keyboard::Key::H: return ImGuiKey_H;
        case sf::Keyboard::Key::I: return ImGuiKey_I;
        case sf::Keyboard::Key::J: return ImGuiKey_J;
        case sf::Keyboard::Key::K: return ImGuiKey_K;
        case sf::Keyboard::Key::L: return ImGuiKey_L;
        case sf::Keyboard::Key::M: return ImGuiKey_M;
        case sf::Keyboard::Key::N: return ImGuiKey_N;
        case sf::Keyboard::Key::O: return ImGuiKey_O;
        case sf::Keyboard::Key::P: return ImGuiKey_P;
        case sf::Keyboard::Key::Q: return ImGuiKey_Q;
        case sf::Keyboard::Key::R: return ImGuiKey_R;
        case sf::Keyboard::Key::S: return ImGuiKey_S;
        case sf::Keyboard::Key::T: return ImGuiKey_T;
        case sf::Keyboard::Key::U: return ImGuiKey_U;
        case sf::Keyboard::Key::V: return ImGuiKey_V;
        case sf::Keyboard::Key::W: return ImGuiKey_W;
        case sf::Keyboard::Key::X: return ImGuiKey_X;
        case sf::Keyboard::Key::Y: return ImGuiKey_Y;
        case sf::Keyboard::Key::Z: return ImGuiKey_Z;
        case sf::Keyboard::Key::Num0: return ImGuiKey_0;
        case sf::Keyboard::Key::Num1: return ImGuiKey_1;
        case sf::Keyboard::Key::Num2: return ImGuiKey_2;
        case sf::Keyboard::Key::Num3: return ImGuiKey_3;
        case sf::Keyboard::Key::Num4: return ImGuiKey_4;
        case sf::Keyboard::Key::Num5: return ImGuiKey_5;
        case sf::Keyboard::Key::Num6: return ImGuiKey_6;
        case sf::Keyboard::Key::Num7: return ImGuiKey_7;
        case sf::Keyboard::Key::Num8: return ImGuiKey_8;
        case sf::Keyboard::Key::Num9: return ImGuiKey_9;
        case sf::Keyboard::Key::Escape: return ImGuiKey_Escape;
        case sf::Keyboard::Key::LControl: return ImGuiKey_LeftCtrl;
        case sf::Keyboard::Key::LShift: return ImGuiKey_LeftShift;
        case sf::Keyboard::Key::LAlt: return ImGuiKey_LeftAlt;
        case sf::Keyboard::Key::RControl: return ImGuiKey_RightCtrl;
        case sf::Keyboard::Key::RShift: return ImGuiKey_RightShift;
        case sf::Keyboard::Key::RAlt: return ImGuiKey_RightAlt;
        case sf::Keyboard::Key::Space: return ImGuiKey_Space;
        case sf::Keyboard::Key::Enter: return ImGuiKey_Enter;
        case sf::Keyboard::Key::Backspace: return ImGuiKey_Backspace;
        case sf::Keyboard::Key::Tab: return ImGuiKey_Tab;
        case sf::Keyboard::Key::Left: return ImGuiKey_LeftArrow;
        case sf::Keyboard::Key::Right: return ImGuiKey_RightArrow;
        case sf::Keyboard::Key::Up: return ImGuiKey_UpArrow;
        case sf::Keyboard::Key::Down: return ImGuiKey_DownArrow;
        default: return ImGuiKey_None;
    }
}

ImGuiLayer::ImGuiLayer() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    refreshDisplayModeOptions();
    buildSceneGraph();
    currentSceneId_ = "splash";
    auto sceneIt = sceneGraph_.find(currentSceneId_);
    if (sceneIt != sceneGraph_.end() && sceneIt->second.onEnter) {
        sceneIt->second.onEnter();
    }
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
        spdlog::warn("Splash texture could not be loaded. Falling back to text-only splash scene.");
    }
}

ImGuiLayer::EventResult ImGuiLayer::processEvent(const sf::Event& event) {
    ImGuiIO& io = ImGui::GetIO();

    EventResult result;

    if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        result.resized = true;
        result.width = resized->size.x;
        result.height = resized->size.y;
        io.DisplaySize = ImVec2(static_cast<float>(result.width), static_cast<float>(result.height));
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

    return result;
}

void ImGuiLayer::setDeltaTime(float deltaSeconds) {
    ImGui::GetIO().DeltaTime = deltaSeconds;
}

void ImGuiLayer::setDisplaySize(uint32_t width, uint32_t height) {
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
}

void ImGuiLayer::setSettings(const AppSettings& settings) {
    settings_ = settings;
    selectedDisplayModeIndex_ = findDisplayModeIndexForSettings(settings_);
}

const AppSettings& ImGuiLayer::settings() const {
    return settings_;
}

void ImGuiLayer::setLoadingComplete() {
    loadingComplete_ = true;
}

void ImGuiLayer::openOptionsScene() {
    queueSceneTransition("options", "Loading options...", 0.2f);
}

void ImGuiLayer::setDisplayConfirmationState(bool active, float secondsRemaining) {
    displayConfirmationActive_ = active;
    displayConfirmationSecondsRemaining_ = std::max(0.0f, secondsRemaining);
}

void ImGuiLayer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

ImGuiLayer::RenderResult ImGuiLayer::renderSceneUi() {
    RenderResult result;

    if (pendingTransition_.active) {
        renderLoadingScene();
        updateSceneTransition();
    } else {
        result = renderCurrentScene();
        if (pendingTransition_.active) {
            renderLoadingScene();
            updateSceneTransition();
        }
    }

    ImGui::Render();
    return result;
}

void ImGuiLayer::renderDrawData(VkCommandBuffer commandBuffer) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void ImGuiLayer::refreshDisplayModeOptions() {
    displayModes_.clear();

#ifdef _WIN32
    std::unordered_set<unsigned long long> dedup;
    DEVMODEW mode{};
    mode.dmSize = sizeof(DEVMODEW);
    for (DWORD modeIndex = 0; EnumDisplaySettingsW(nullptr, modeIndex, &mode) != 0; ++modeIndex) {
        if (mode.dmPelsWidth == 0 || mode.dmPelsHeight == 0 || mode.dmDisplayFrequency == 0) {
            continue;
        }

        const unsigned long long key =
            (static_cast<unsigned long long>(mode.dmPelsWidth) << 40) |
            (static_cast<unsigned long long>(mode.dmPelsHeight) << 20) |
            static_cast<unsigned long long>(mode.dmDisplayFrequency);

        if (dedup.insert(key).second) {
            displayModes_.push_back({
                static_cast<int>(mode.dmPelsWidth),
                static_cast<int>(mode.dmPelsHeight),
                static_cast<int>(mode.dmDisplayFrequency)});
        }
    }
#endif

    if (displayModes_.empty()) {
        const auto& fullscreenModes = sf::VideoMode::getFullscreenModes();
        for (const auto& mode : fullscreenModes) {
            displayModes_.push_back({
                static_cast<int>(mode.size.x),
                static_cast<int>(mode.size.y),
                60});
        }
    }

    if (displayModes_.empty()) {
        displayModes_.push_back({1280, 720, 60});
    }

    std::sort(displayModes_.begin(), displayModes_.end(), [](const DisplayModeOption& left, const DisplayModeOption& right) {
        if (left.width != right.width) {
            return left.width > right.width;
        }
        if (left.height != right.height) {
            return left.height > right.height;
        }
        return left.refreshRate > right.refreshRate;
    });
}

int ImGuiLayer::findDisplayModeIndexForSettings(const AppSettings& settings) const {
    for (size_t i = 0; i < displayModes_.size(); ++i) {
        const DisplayModeOption& mode = displayModes_[i];
        if (mode.width == settings.displayWidth && mode.height == settings.displayHeight && mode.refreshRate == settings.refreshRate) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

std::string ImGuiLayer::modeLabel(const DisplayModeOption& mode) const {
    std::ostringstream stream;
    stream << mode.width << " x " << mode.height << " @ " << mode.refreshRate << " Hz";
    return stream.str();
}

void ImGuiLayer::buildSceneGraph() {
    sceneGraph_.clear();

    sceneGraph_.emplace("splash", SceneNode{
        .id = "splash",
        .onEnter = [this]() { splashElapsedSeconds_ = 0.0f; },
        .onExit = []() {},
        .onRender = [this]() {
            renderSplashScene();
            return RenderResult{};
        }});

    sceneGraph_.emplace("main_menu", SceneNode{
        .id = "main_menu",
        .onEnter = []() {},
        .onExit = []() {},
        .onRender = [this]() { return renderMainMenuScene(); }});

    sceneGraph_.emplace("options", SceneNode{
        .id = "options",
        .onEnter = []() {},
        .onExit = []() {},
        .onRender = [this]() { return renderOptionsScene(); }});

    sceneGraph_.emplace("level_select", SceneNode{
        .id = "level_select",
        .onEnter = [this]() {
            if (availableLevels_.empty()) {
                availableLevels_.push_back("Training Field");
            }
            selectedLevelIndex_ = std::clamp(selectedLevelIndex_, 0, static_cast<int>(availableLevels_.size() - 1));
        },
        .onExit = []() {},
        .onRender = [this]() { return renderLevelSelectionScene(); }});

    sceneGraph_.emplace("simulation", SceneNode{
        .id = "simulation",
        .onEnter = [this]() { simulationRemainingSeconds_ = 5.0f; },
        .onExit = []() {},
        .onRender = [this]() {
            renderSimulationScene();
            return RenderResult{};
        }});
}

void ImGuiLayer::queueSceneTransition(const std::string& targetSceneId, const std::string& loadingMessage, float minDurationSeconds) {
    if (targetSceneId.empty()) {
        return;
    }

    if (!pendingTransition_.active && currentSceneId_ == targetSceneId) {
        return;
    }

    pendingTransition_.active = true;
    pendingTransition_.targetSceneId = targetSceneId;
    pendingTransition_.loadingMessage = loadingMessage.empty() ? "Loading..." : loadingMessage;
    pendingTransition_.elapsedSeconds = 0.0f;
    pendingTransition_.minDurationSeconds = std::max(0.05f, minDurationSeconds);
}

void ImGuiLayer::updateSceneTransition() {
    if (!pendingTransition_.active) {
        return;
    }

    pendingTransition_.elapsedSeconds += ImGui::GetIO().DeltaTime;
    if (pendingTransition_.elapsedSeconds < pendingTransition_.minDurationSeconds) {
        return;
    }

    const auto targetIt = sceneGraph_.find(pendingTransition_.targetSceneId);
    if (targetIt == sceneGraph_.end()) {
        spdlog::warn("Scene transition requested unknown scene: {}", pendingTransition_.targetSceneId);
        pendingTransition_ = {};
        return;
    }

    const auto currentIt = sceneGraph_.find(currentSceneId_);
    if (currentIt != sceneGraph_.end() && currentIt->second.onExit) {
        currentIt->second.onExit();
    }

    currentSceneId_ = pendingTransition_.targetSceneId;
    if (targetIt->second.onEnter) {
        targetIt->second.onEnter();
    }

    pendingTransition_ = {};
}

void ImGuiLayer::renderLoadingScene() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags loadingWindowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("SceneLoading", nullptr, loadingWindowFlags);

    if (splashTexture_.has_value() && splashTexture_->isValid()) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float scale = std::min(available.x / static_cast<float>(splashTexture_->width()), available.y / static_cast<float>(splashTexture_->height()));
        const ImVec2 imageSize{static_cast<float>(splashTexture_->width()) * scale * 0.8f, static_cast<float>(splashTexture_->height()) * scale * 0.8f};

        const float cursorX = std::max(0.0f, (available.x - imageSize.x) * 0.5f);
        const float cursorY = std::max(0.0f, (available.y - imageSize.y) * 0.35f);
        ImGui::SetCursorPos(ImVec2(cursorX, cursorY));
        ImGui::Image(splashTexture_->textureRef(), imageSize);
    }

    const float progress = pendingTransition_.minDurationSeconds > 0.0f
        ? std::clamp(pendingTransition_.elapsedSeconds / pendingTransition_.minDurationSeconds, 0.0f, 1.0f)
        : 1.0f;

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 90.0f);
    ImGui::TextUnformatted(pendingTransition_.loadingMessage.c_str());
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));

    ImGui::End();
}

ImGuiLayer::RenderResult ImGuiLayer::renderCurrentScene() {
    const auto sceneIt = sceneGraph_.find(currentSceneId_);
    if (sceneIt == sceneGraph_.end() || !sceneIt->second.onRender) {
        return {};
    }

    return sceneIt->second.onRender();
}

void ImGuiLayer::renderSplashScene() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags splashWindowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("SplashScreen", nullptr, splashWindowFlags);

    if (splashTexture_.has_value() && splashTexture_->isValid()) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float scale = std::min(available.x / static_cast<float>(splashTexture_->width()), available.y / static_cast<float>(splashTexture_->height()));
        const ImVec2 imageSize{static_cast<float>(splashTexture_->width()) * scale, static_cast<float>(splashTexture_->height()) * scale};

        const float cursorX = std::max(0.0f, (available.x - imageSize.x) * 0.5f);
        const float cursorY = std::max(0.0f, (available.y - imageSize.y) * 0.45f);
        ImGui::SetCursorPos(ImVec2(cursorX, cursorY));
        ImGui::Image(splashTexture_->textureRef(), imageSize);
    } else {
        ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.45f);
        ImGui::TextWrapped("Loading splash_screen.png...");
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 50.0f);
    ImGui::TextUnformatted("Initializing systems...");

    ImGui::End();

    splashElapsedSeconds_ += ImGui::GetIO().DeltaTime;
    if (loadingComplete_ && splashElapsedSeconds_ >= splashMinimumSeconds_ && !pendingTransition_.active) {
        queueSceneTransition("main_menu", "Loading main menu...", 0.3f);
    }
}

ImGuiLayer::RenderResult ImGuiLayer::renderMainMenuScene() {
    RenderResult result;

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 menuSize{340.0f, 280.0f};
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - menuSize.x) * 0.5f, (displaySize.y - menuSize.y) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(menuSize, ImGuiCond_Always);

    ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Spacing();
    ImGui::TextUnformatted("NodeSpireTD");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Play", ImVec2(-1.0f, 0.0f))) {
        queueSceneTransition("level_select", "Loading level selection...", 0.5f);
    }

    if (ImGui::Button("Options", ImVec2(-1.0f, 0.0f))) {
        queueSceneTransition("options", "Loading options...", 0.3f);
    }

    if (ImGui::Button("Quit", ImVec2(-1.0f, 0.0f))) {
        result.requestQuit = true;
    }

    ImGui::End();
    return result;
}

ImGuiLayer::RenderResult ImGuiLayer::renderOptionsScene() {
    RenderResult result;

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 optionsSize{500.0f, 400.0f};
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - optionsSize.x) * 0.5f, (displaySize.y - optionsSize.y) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(optionsSize, ImGuiCond_Always);

    ImGui::Begin("Options", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::Checkbox("Fullscreen", &settings_.fullscreen);
    ImGui::Checkbox("V-Sync", &settings_.vSyncEnabled);

    if (!displayModes_.empty()) {
        const std::string preview = modeLabel(displayModes_[std::clamp(selectedDisplayModeIndex_, 0, static_cast<int>(displayModes_.size() - 1))]);
        if (ImGui::BeginCombo("Display Mode", preview.c_str())) {
            for (int i = 0; i < static_cast<int>(displayModes_.size()); ++i) {
                const bool selected = (selectedDisplayModeIndex_ == i);
                const std::string label = modeLabel(displayModes_[i]);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    selectedDisplayModeIndex_ = i;
                    settings_.displayWidth = displayModes_[i].width;
                    settings_.displayHeight = displayModes_[i].height;
                    settings_.refreshRate = displayModes_[i].refreshRate;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SliderInt("Graphics Quality", &settings_.graphicsQuality, 0, 3);

    ImGui::Separator();
    ImGui::TextUnformatted("Sound");
    ImGui::SliderFloat("Master Volume", &settings_.masterVolume, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Music Volume", &settings_.musicVolume, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("SFX Volume", &settings_.sfxVolume, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("Mute when unfocused", &settings_.muteWhenUnfocused);

    if (displayConfirmationActive_) {
        ImGui::OpenPopup("Confirm Display Changes");
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 170.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Confirm Display Changes", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("Keep these display settings? They will be reverted automatically if not confirmed.");
        ImGui::Spacing();
        ImGui::Text("Reverting in %.1f seconds", displayConfirmationSecondsRemaining_);
        ImGui::Separator();

        if (ImGui::Button("Accept Changes", ImVec2(170.0f, 0.0f))) {
            result.requestAcceptDisplayChanges = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert", ImVec2(120.0f, 0.0f))) {
            result.requestRevertDisplayChanges = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::Separator();
    if (ImGui::Button("Apply", ImVec2(120.0f, 0.0f))) {
        result.requestApplySettings = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(120.0f, 0.0f))) {
        queueSceneTransition("main_menu", "Returning to main menu...", 0.2f);
    }

    ImGui::End();
    return result;
}

ImGuiLayer::RenderResult ImGuiLayer::renderLevelSelectionScene() {
    RenderResult result;

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 windowSize{560.0f, 400.0f};
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - windowSize.x) * 0.5f, (displaySize.y - windowSize.y) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

    ImGui::Begin("Level Selection", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::TextUnformatted("Select a mission profile");
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(availableLevels_.size()); ++i) {
        const bool selected = (selectedLevelIndex_ == i);
        if (ImGui::Selectable(availableLevels_[i].c_str(), selected)) {
            selectedLevelIndex_ = i;
        }
    }

    ImGui::Spacing();
    if (!availableLevels_.empty()) {
        activeLevelName_ = availableLevels_[selectedLevelIndex_];
        ImGui::Text("Selected: %s", activeLevelName_.c_str());
    }

    ImGui::Separator();
    if (ImGui::Button("Load Level", ImVec2(140.0f, 0.0f))) {
        queueSceneTransition("simulation", "Loading level: " + activeLevelName_ + "...", 1.2f);
    }

    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(120.0f, 0.0f))) {
        queueSceneTransition("main_menu", "Returning to main menu...", 0.2f);
    }

    ImGui::End();
    return result;
}

void ImGuiLayer::renderSimulationScene() {
    simulationRemainingSeconds_ -= ImGui::GetIO().DeltaTime;
    if (simulationRemainingSeconds_ <= 0.0f && !pendingTransition_.active) {
        simulationRemainingSeconds_ = 5.0f;
        queueSceneTransition("main_menu", "Returning to main menu...", 0.4f);
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 simulationSize{460.0f, 240.0f};
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - simulationSize.x) * 0.5f, (displaySize.y - simulationSize.y) * 0.5f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(simulationSize, ImGuiCond_Always);

    ImGui::Begin("Simulation", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Running level: %s", activeLevelName_.c_str());
    ImGui::Separator();
    ImGui::Text("Returning to menu in %.1f seconds", std::max(0.0f, simulationRemainingSeconds_));
    ImGui::ProgressBar(1.0f - (std::max(0.0f, simulationRemainingSeconds_) / 5.0f), ImVec2(-1.0f, 0.0f));
    ImGui::End();
}
