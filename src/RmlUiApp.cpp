// RmlUi-based app bootstrap: SDL3+Vulkan backend (RmlUi's own, not yet
// reconciled with VulkanContext -- see rmlui-sdl-vulkan-backend skill notes)
// with the Lua scripting plugin wired up, driving scenes via SceneManager.
#include "RmlUiFontLoader.hpp"
#include "rmlui/SceneManager.hpp"
#include "rmlui/SceneTypes.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi/Lua.h>
#include <RmlUi_Backend.h>
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
        const NodeSpireUi::SceneId before = g_sceneManager->activeSceneId();
        g_sceneManager->handleKeyDown(key);
        if (g_sceneManager->activeSceneId() != before)
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

    Rml::Context* context = Rml::CreateContext("main", Rml::Vector2i(windowWidth, windowHeight));
    if (!context)
    {
        Rml::Shutdown();
        Backend::Shutdown();
        return -1;
    }

    Rml::Debugger::Initialise(context);

    if (!RmlUiFontLoader::LoadAll(NODESPIRE_ASSET_ROOT "/fonts"))
        Rml::Log::Message(Rml::Log::LT_WARNING, "One or more fonts failed to load from %s", NODESPIRE_ASSET_ROOT "/fonts");

    NodeSpireUi::SceneManager sceneManager(*context, NodeSpireUi::SceneId::Splash);
    g_sceneManager = &sceneManager;

    double lastElapsedTime = systemInterface.GetElapsedTime();

    bool running = true;
    while (running)
    {
        running = Backend::ProcessEvents(context, ProcessKeyDownShortcuts, true);

        const double elapsedTime = systemInterface.GetElapsedTime();
        const float dt = static_cast<float>(elapsedTime - lastElapsedTime);
        lastElapsedTime = elapsedTime;
        sceneManager.update(dt);

        context->Update();

        Backend::BeginFrame();
        context->Render();
        Backend::PresentFrame();
    }

    g_sceneManager = nullptr;

    Rml::Shutdown();
    Backend::Shutdown();

    return 0;
}
