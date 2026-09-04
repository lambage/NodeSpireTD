// Minimal RmlUi bring-up smoke test: SDL3+Vulkan backend with the Lua scripting
// plugin wired up, no scene/native Lua-VM integration yet.
#include "RmlUiFontLoader.hpp"

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

bool ProcessKeyDownShortcuts(Rml::Context* /*context*/, Rml::Input::KeyIdentifier key, int /*key_modifier*/, float /*native_dp_ratio*/, bool priority)
{
    if (priority && key == Rml::Input::KI_F8)
    {
        Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
        return false;
    }
    return true;
}
} // namespace

int main(int /*argc*/, char** /*argv*/)
{
    const int windowWidth = 1024;
    const int windowHeight = 768;

    if (!Backend::Initialize("NodeSpireTD - RmlUi Hello World", windowWidth, windowHeight, true))
        return -1;

    SpdlogSystemInterface systemInterface(Backend::GetSystemInterface());
    Rml::SetSystemInterface(&systemInterface);
    Rml::SetRenderInterface(Backend::GetRenderInterface());

    Rml::Initialise();

    // Registers the Lua scripting plugin so hello.rml's <script> block and
    // onclick="..." attributes are interpreted as Lua.
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

    if (Rml::ElementDocument* document = context->LoadDocument(NODESPIRE_ASSET_ROOT "/ui/hello/hello.rml"))
        document->Show();
    else
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load document: %s", NODESPIRE_ASSET_ROOT "/ui/hello/hello.rml");

    bool running = true;
    while (running)
    {
        running = Backend::ProcessEvents(context, ProcessKeyDownShortcuts, true);

        context->Update();

        Backend::BeginFrame();
        context->Render();
        Backend::PresentFrame();
    }

    Rml::Shutdown();
    Backend::Shutdown();

    return 0;
}
