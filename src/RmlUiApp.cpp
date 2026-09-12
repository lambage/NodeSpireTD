// RmlUi-based app bootstrap using SDL3 and the game-owned VulkanContext.
#include "AudioEngine.hpp"
#include "RmlUiFontLoader.hpp"
#include "SettingsManager.hpp"
#include "VulkanContext.hpp"
#include "multiplayer/MultiplayerSession.hpp"
#include "multiplayer/PlayerProfileStore.hpp"
#include "rmlui/SceneManager.hpp"
#include "rmlui/SceneTypes.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi/Lua.h>
#include <RmlUi_Backend.h>
#include <memory>
#include <spdlog/spdlog.h>

namespace
{
// Forwards all platform behavior to the backend's SDL system interface, but routes
// LogMessage through spdlog so RmlUi's internal warnings/errors are reliably visible.
class SpdlogSystemInterface : public Rml::SystemInterface
{
public:
    explicit SpdlogSystemInterface(Rml::SystemInterface* inner) : m_inner(inner) {}

    double GetElapsedTime() override { return m_inner->GetElapsedTime(); }
    int TranslateString(Rml::String& translated, const Rml::String& input) override { return m_inner->TranslateString(translated, input); }
    void JoinPath(Rml::String& translated_path, const Rml::String& document_path, const Rml::String& path) override
    {
        m_inner->JoinPath(translated_path, document_path, path);
    }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
    {
        switch (type)
        {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            spdlog::error("[RmlUi] {}", message);
            break;
        case Rml::Log::LT_WARNING:
            spdlog::warn("[RmlUi] {}", message);
            break;
        case Rml::Log::LT_INFO:
        case Rml::Log::LT_ALWAYS:
            spdlog::info("[RmlUi] {}", message);
            break;
        default:
            spdlog::debug("[RmlUi] {}", message);
            break;
        }
        return true;
    }
    void SetMouseCursor(const Rml::String& cursor_name) override { m_inner->SetMouseCursor(cursor_name); }
    void SetClipboardText(const Rml::String& text) override { m_inner->SetClipboardText(text); }
    void GetClipboardText(Rml::String& text) override { m_inner->GetClipboardText(text); }
    void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override { m_inner->ActivateKeyboard(caret_position, line_height); }
    void DeactivateKeyboard() override { m_inner->DeactivateKeyboard(); }

private:
    Rml::SystemInterface* m_inner;
};

NodeSpireUi::SceneManager* g_sceneManager = nullptr;

bool ProcessKeyDownShortcuts(Rml::Context* /*context*/, Rml::Input::KeyIdentifier key, int /*key_modifier*/, float /*native_dp_ratio*/, bool priority)
{
    if (priority && key == Rml::Input::KI_F8)
    {
        Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
        return false;
    }

    // Lower-priority shortcuts: only consume the key if it actually caused a
    // scene transition, so unrelated keys still reach the RmlUi context.
    if (!priority && g_sceneManager)
    {
        if (g_sceneManager->handleKeyDown(key))
            return false;
    }
    return true;
}
} // namespace

int main(int /*argc*/, char** /*argv*/)
{
    const int windowWidth = 1024;
    const int windowHeight = 768;

    if (!Backend::Initialize("NodeSpireTD", windowWidth, windowHeight, true))
        return -1;

    auto vulkanContext = std::make_unique<VulkanContext>(Backend::GetWindow());
    if (!Backend::InitializeRenderer(*vulkanContext))
    {
        vulkanContext.reset();
        Backend::Shutdown();
        return -1;
    }

    SpdlogSystemInterface systemInterface(Backend::GetSystemInterface());
    Rml::SetSystemInterface(&systemInterface);
    Rml::SetRenderInterface(Backend::GetRenderInterface());

    Rml::Initialise();

    // Registers the Lua scripting plugin so any document's <script> block and
    // onclick="..." attributes are interpreted as Lua. Owns its own global
    // Lua state (see LuaPlugin::OnInitialise) -- RmlUi's Lua plugin is a
    // process-wide singleton, not a per-scene VM; see
    // rmlui-lua-scene-integration skill notes for why the legacy
    // one-Lua-VM-per-scene model doesn't carry over as-is.
    Rml::Lua::Initialise();

    const VkExtent2D initialExtent = vulkanContext->extent();
    Rml::Context* context = Rml::CreateContext(
        "main", Rml::Vector2i(static_cast<int>(initialExtent.width), static_cast<int>(initialExtent.height)));
    if (!context)
    {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create Rml::Context");
        Rml::Shutdown();
        Backend::ShutdownRenderer();
        vulkanContext.reset();
        Backend::Shutdown();
        return -1;
    }

    Rml::Debugger::Initialise(context);

    if (!RmlUiFontLoader::LoadAll("assets/fonts")) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "One or more fonts failed to load from %s", "assets/fonts");
    }    

    {
        const AppSettings startupSettings = SettingsManager().loadOrCreateDefaults();
        AudioEngine audioEngine(startupSettings.audioDevice);
        audioEngine.setEffectiveSettings(startupSettings);
        multiplayer::MultiplayerSession multiplayerSession;
        multiplayer::PlayerProfileStore playerProfileStore;

        NodeSpireUi::SceneManager sceneManager(*context, NodeSpireUi::SceneId::Splash, audioEngine, *vulkanContext, multiplayerSession,
                                               playerProfileStore);
        g_sceneManager = &sceneManager;

        double lastElapsedTime = systemInterface.GetElapsedTime();

        bool running = true;
        size_t frameIndex = 0;
        while (running)
        {
            // Bound the event wait so party traffic advances without focus while rendering stays paced.
            running = Backend::ProcessEvents(context, ProcessKeyDownShortcuts, true, 1.0 / 60.0);

            const double elapsedTime = systemInterface.GetElapsedTime();
            const float dt = static_cast<float>(elapsedTime - lastElapsedTime);
            lastElapsedTime = elapsedTime;
            multiplayerSession.update();
            sceneManager.update(dt);
            audioEngine.update(dt);

            context->Update();

            vulkanContext->waitForFrameFence(frameIndex);
            uint32_t imageIndex = 0;
            if (vulkanContext->acquireNextImage(frameIndex, imageIndex) == VulkanContext::AcquireStatus::OutOfDate)
            {
                const VkExtent2D extent = vulkanContext->extent();
                vulkanContext->recreateSwapchain(extent.width, extent.height);
                continue;
            }

            VkCommandBuffer commandBuffer = vulkanContext->beginFrameRecording(frameIndex, imageIndex);
            sceneManager.renderWorld(commandBuffer, vulkanContext->extent());
            Backend::BeginFrame(commandBuffer, static_cast<uint32_t>(frameIndex));
            context->Render();
            Backend::PresentFrame();
            sceneManager.renderOverlay(commandBuffer, vulkanContext->extent());
            vulkanContext->endFrameRecordingAndSubmit(frameIndex, imageIndex, commandBuffer);
            if (vulkanContext->present(imageIndex))
            {
                const VkExtent2D extent = vulkanContext->extent();
                vulkanContext->recreateSwapchain(extent.width, extent.height);
            }
            frameIndex = (frameIndex + 1) % VulkanContext::kMaxFramesInFlight;
        }

        g_sceneManager = nullptr;

        vulkanContext->waitIdle();
        sceneManager.shutdown();
    }

    Rml::Shutdown();
    Backend::ShutdownRenderer();
    vulkanContext.reset();
    Backend::Shutdown();

    return 0;
}
