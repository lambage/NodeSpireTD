#pragma once

// Forked from RmlUi's Backends/RmlUi_Backend.h (SDL_VK sample backend, MIT
// licensed, see build/_deps/rmlui-src/LICENSE.txt) to add live display/vsync
// mutators needed by the Options scene. This header shadows the vendored one
// (src/rmlui_backend is listed before Backends/ in CMakeLists.txt's include
// dirs) so both quoted and angle-bracket #include "RmlUi_Backend.h" resolve
// here.

#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>
#include <cstdint>
#include <volk.h>

struct SDL_Window;
class VulkanContext;

using KeyDownCallback = bool (*)(Rml::Context* context, Rml::Input::KeyIdentifier key, int key_modifier, float native_dp_ratio, bool priority);

/**
    This interface serves as a basic abstraction over the various backends included with RmlUi. It is mainly intended as an example to get something
    simple up and running, and provides just enough functionality for the included samples.

    This interface may be used directly for simple applications and testing. However, for anything more advanced we recommend to use the backend as a
    starting point and copy relevant parts into the main loop of your application. On the other hand, the underlying platform and renderer used by the
    backend are intended to be re-usable as is.
 */
namespace Backend {

// Initializes the backend, including the custom system and render interfaces, and opens a window for rendering the RmlUi context.
bool Initialize(const char* window_name, int width, int height, bool allow_resize);
bool InitializeRenderer(VulkanContext& vulkan_context);
void ShutdownRenderer();
// Closes the window and release all resources owned by the backend, including the system and render interfaces.
void Shutdown();

// Returns a pointer to the custom system interface which should be provided to RmlUi.
Rml::SystemInterface* GetSystemInterface();
// Returns a pointer to the custom render interface which should be provided to RmlUi.
Rml::RenderInterface* GetRenderInterface();
// Returns the SDL window owned by the platform backend. The application Vulkan context consumes
// this window when it becomes the sole surface/device owner.
SDL_Window* GetWindow();

// Polls and processes events from the current platform, and applies any relevant events to the provided RmlUi context and the key down callback.
// @return False to indicate that the application should be closed.
bool ProcessEvents(Rml::Context* context, KeyDownCallback key_down_callback = nullptr, bool power_save = false,
    double max_wait_seconds = 10.0);
// Request application closure during the next event processing call.
void RequestExit();

// Prepares the render state to accept rendering commands from RmlUi, call before rendering the RmlUi context.
void BeginFrame(VkCommandBuffer command_buffer, uint32_t frame_index);
// Presents the rendered frame to the screen, call after rendering the RmlUi context.
void PresentFrame();

// Applies display settings to the live window. When fullscreen is false, resizes the window to
// width/height. When true, uses the closest matching exclusive display mode (if exclusive_fullscreen)
// or borderless-desktop fullscreen otherwise. Synchronously resizes the swapchain and the given
// context's dimensions to the resulting window size, rather than waiting for the next SDL resize
// event (which would otherwise leave a stale, smaller viewport for a frame or more).
void ApplyDisplaySettings(Rml::Context& context, bool fullscreen, bool exclusive_fullscreen, int width, int height, int refresh_rate);
// Enables or disables vertical sync, recreating the swapchain if the setting actually changed.
void SetVSyncEnabled(bool enabled);

} // namespace Backend
