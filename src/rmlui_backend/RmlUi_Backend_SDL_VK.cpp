// Forked from RmlUi's Backends/RmlUi_Backend_SDL_VK.cpp (SDL_VK sample backend, MIT
// licensed, see build/_deps/rmlui-src/LICENSE.txt). Unmodified except for this notice;
// ties together our forked RmlUi_Platform_SDL/RmlUi_Renderer_VK translation units.
#include "RmlUi_Backend.h"
#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_VK.h"
#include "VulkanContext.hpp"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>

#if SDL_MAJOR_VERSION == 2 && !(SDL_VIDEO_VULKAN)
	#error "Only the Vulkan SDL backend is supported."
#endif

#if SDL_MAJOR_VERSION >= 3
	#include <SDL3/SDL_vulkan.h>
#else
	#include <SDL2/SDL_vulkan.h>
#endif

/**
    Global data used by this backend.

    Lifetime governed by the calls to Backend::Initialize() and Backend::Shutdown().
 */
struct BackendData {
	BackendData(SDL_Window* window) : system_interface(window) {}

	SystemInterface_SDL system_interface;
	RenderInterface_VK render_interface;
	TextInputMethodEditor_SDL text_input_method_editor;

	SDL_Window* window = nullptr;
	VulkanContext* vulkan_context = nullptr;
	bool renderer_initialized = false;

	bool running = true;
};
static Rml::UniquePtr<BackendData> data;

bool Backend::Initialize(const char* window_name, int width, int height, bool allow_resize)
{
	RMLUI_ASSERT(!data);

#if SDL_MAJOR_VERSION >= 3
	SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
		return false;
#else
	SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0)
		return false;
#endif

	// Submit click events when focusing the window.
	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
	// Touch events are handled natively, no need to generate synthetic mouse events for touch devices.
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

#if defined RMLUI_BACKEND_SIMULATE_TOUCH
	// Simulate touch events from mouse events for testing touch behavior on a desktop machine.
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
#endif

#if SDL_MAJOR_VERSION >= 3
	const float window_size_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, window_name);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, int(width * window_size_scale));
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, int(height * window_size_scale));
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, allow_resize);
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
	SDL_Window* window = SDL_CreateWindowWithProperties(props);
	SDL_DestroyProperties(props);
#else
	const Uint32 window_flags = (SDL_WINDOW_VULKAN | (allow_resize ? SDL_WINDOW_RESIZABLE : 0));
	SDL_Window* window = SDL_CreateWindow(window_name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, window_flags);
	// SDL2 implicitly activates text input on window creation. Turn it off for now, it will be activated again e.g. when focusing a text input field.
	SDL_StopTextInput();
#endif

	if (!window)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "SDL error on create window: %s", SDL_GetError());
		return false;
	}

	data = Rml::MakeUnique<BackendData>(window);
	data->window = window;

	Rml::SetTextInputHandler(&data->text_input_method_editor);

	return true;
}

bool Backend::InitializeRenderer(VulkanContext& vulkan_context)
{
	RMLUI_ASSERT(data && !data->renderer_initialized);
	data->vulkan_context = &vulkan_context;
	const VkExtent2D extent = vulkan_context.extent();
	data->renderer_initialized = data->render_interface.Initialize(vulkan_context.instance(), vulkan_context.physicalDevice(),
		vulkan_context.device(), vulkan_context.graphicsQueue(), vulkan_context.graphicsQueueFamily(), vulkan_context.allocator(),
		vulkan_context.swapchainColorFormat(), vulkan_context.depthFormat(), extent);
	return data->renderer_initialized;
}

void Backend::ShutdownRenderer()
{
	RMLUI_ASSERT(data);
	if (data->renderer_initialized)
	{
		data->render_interface.Shutdown();
		data->renderer_initialized = false;
		data->vulkan_context = nullptr;
	}
}

void Backend::Shutdown()
{
	RMLUI_ASSERT(data);

	ShutdownRenderer();

	SDL_DestroyWindow(data->window);

	data.reset();

	SDL_Quit();
}

Rml::SystemInterface* Backend::GetSystemInterface()
{
	RMLUI_ASSERT(data);
	return &data->system_interface;
}

Rml::RenderInterface* Backend::GetRenderInterface()
{
	RMLUI_ASSERT(data);
	return &data->render_interface;
}

SDL_Window* Backend::GetWindow()
{
	RMLUI_ASSERT(data);
	return data->window;
}

static void SynchronizeWindowSize(Rml::Context* context)
{
	int pixel_width = 0;
	int pixel_height = 0;
	SDL_GetWindowSizeInPixels(data->window, &pixel_width, &pixel_height);
	if (pixel_width > 0 && pixel_height > 0)
	{
		if (data->vulkan_context)
			data->vulkan_context->recreateSwapchain(static_cast<uint32_t>(pixel_width), static_cast<uint32_t>(pixel_height));
		data->render_interface.SetViewport(pixel_width, pixel_height);
		if (context)
			context->SetDimensions({pixel_width, pixel_height});
	}
}

bool Backend::ProcessEvents(Rml::Context* context, KeyDownCallback key_down_callback, bool power_save, double max_wait_seconds)
{
	RMLUI_ASSERT(data && context);

#if SDL_MAJOR_VERSION >= 3
	#define RMLSDL_WINDOW_EVENTS_BEGIN
	#define RMLSDL_WINDOW_EVENTS_END
	auto GetKey = [](const SDL_Event& event) { return event.key.key; };
	auto GetDisplayScale = []() { return SDL_GetWindowDisplayScale(data->window); };
	constexpr auto event_quit = SDL_EVENT_QUIT;
	constexpr auto event_key_down = SDL_EVENT_KEY_DOWN;
	constexpr auto event_text_editing = SDL_EVENT_TEXT_EDITING;
	constexpr auto event_window_size_changed = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
	bool has_event = false;
#else
	#define RMLSDL_WINDOW_EVENTS_BEGIN \
	case SDL_WINDOWEVENT:              \
	{                                  \
		switch (ev.window.event)       \
		{
	#define RMLSDL_WINDOW_EVENTS_END \
		}                            \
		}                            \
		break;
	auto GetKey = [](const SDL_Event& event) { return event.key.keysym.sym; };
	auto GetDisplayScale = []() { return 1.f; };
	constexpr auto event_quit = SDL_QUIT;
	constexpr auto event_key_down = SDL_KEYDOWN;
	constexpr auto event_text_editing = SDL_TEXTEDITING;
	constexpr auto event_window_size_changed = SDL_WINDOWEVENT_SIZE_CHANGED;
	int has_event = 0;
#endif

	bool result = data->running;
	data->running = true;

	SDL_Event ev;
	if (power_save)
		has_event = SDL_WaitEventTimeout(&ev, static_cast<int>(Rml::Math::Min(context->GetNextUpdateDelay(), max_wait_seconds) * 1000));
	else
		has_event = SDL_PollEvent(&ev);

	while (has_event)
	{
		bool propagate_event = true;
		switch (ev.type)
		{
		case event_quit:
		{
			propagate_event = false;
			result = false;
		}
		break;
		case event_key_down:
		{
			propagate_event = false;
			const Rml::Input::KeyIdentifier key = RmlSDL::ConvertKey(GetKey(ev));
			const int key_modifier = RmlSDL::GetKeyModifierState();
			const float native_dp_ratio = GetDisplayScale();

			// See if we have any global shortcuts that take priority over the context.
			if (key_down_callback && !key_down_callback(context, key, key_modifier, native_dp_ratio, true))
				break;
			// Otherwise, hand the event over to the context by calling the input handler as normal.
			if (!RmlSDL::InputEventHandler(context, data->window, ev))
				break;
			// The key was not consumed by the context either, try keyboard shortcuts of lower priority.
			if (key_down_callback && !key_down_callback(context, key, key_modifier, native_dp_ratio, false))
				break;
		}
		break;
		case event_text_editing:
		{
			propagate_event = false;
			data->text_input_method_editor.HandleEdit(ev.edit);
		}
		break;

			RMLSDL_WINDOW_EVENTS_BEGIN

	#if SDL_MAJOR_VERSION >= 3
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
		case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
	#endif
		case event_window_size_changed:
		{
			SynchronizeWindowSize(context);
		}
		break;

			RMLSDL_WINDOW_EVENTS_END

		default: break;
		}

		if (propagate_event)
			RmlSDL::InputEventHandler(context, data->window, ev);

		has_event = SDL_PollEvent(&ev);
	}

	return result;
}

void Backend::RequestExit()
{
	RMLUI_ASSERT(data);
	data->running = false;
}

void Backend::ApplyDisplaySettings(Rml::Context& context, bool fullscreen, bool exclusive_fullscreen, int width, int height, int refresh_rate)
{
	RMLUI_ASSERT(data);

	if (!fullscreen)
	{
		SDL_SetWindowFullscreen(data->window, false);
		SDL_SetWindowSize(data->window, width, height);
	}
	else
	{
		if (exclusive_fullscreen)
		{
			SDL_DisplayMode closest{};
			if (SDL_GetClosestFullscreenDisplayMode(SDL_GetPrimaryDisplay(), width, height, static_cast<float>(refresh_rate), false, &closest))
				SDL_SetWindowFullscreenMode(data->window, &closest);
		}
		else
		{
			// Null mode means borderless-desktop fullscreen at the display's current mode.
			SDL_SetWindowFullscreenMode(data->window, nullptr);
		}

		SDL_SetWindowFullscreen(data->window, true);
	}

	// SDL applies most of this synchronously, but on some platforms the mode switch finishes on a
	// later event-loop iteration; block until it's done so the size we query below is final.
	SDL_SyncWindow(data->window);

	// The resulting SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED is only picked up on the next ProcessEvents()
	// call, which would leave the swapchain/context sized to the old window for a frame (visually:
	// only the old window's pixel footprint gets rendered into the new, larger window). Query and
	// apply the real size immediately instead of waiting for that round-trip.
	SynchronizeWindowSize(&context);
}

void Backend::SetVSyncEnabled(bool enabled)
{
	RMLUI_ASSERT(data);
	if (data->vulkan_context)
		data->vulkan_context->setVSyncEnabled(enabled);
}

void Backend::BeginFrame(VkCommandBuffer command_buffer, uint32_t frame_index)
{
	RMLUI_ASSERT(data && data->vulkan_context);
	data->render_interface.BeginFrame(command_buffer, data->vulkan_context->extent(), frame_index);
}

void Backend::PresentFrame()
{
	RMLUI_ASSERT(data);
	data->render_interface.EndFrame();
}
