---
name: sdl3-windowing-migration
description: 'Use when replacing SFML 3.1 windowing, event polling, keyboard/mouse input, or Vulkan-surface creation with SDL3 in NodeSpireTD — specifically AppController.hpp/.cpp (owns sf::Window), VulkanContext.hpp/.cpp (constructed from sf::Window&, calls window.createVulkanSurface), and ImGuiLayer''s SFML event translation (to be replaced by RmlUi input handling). Covers the CMake FetchContent swap from SFML to SDL3 and the concrete API mapping for each call site.'
---

# SDL3 Windowing Migration (from SFML)

## Known call sites to replace (verify these still match before editing — code may have moved)
- [src/AppController.cpp](../../../src/AppController.cpp): owns `sf::Window window_` (~line 260),
  exposes `window()` accessor (~line 247). This becomes the `SDL_Window*` owner.
- [src/VulkanContext.hpp](../../../src/VulkanContext.hpp) / [.cpp](../../../src/VulkanContext.cpp):
  constructor takes `sf::Window&` (stored as `window_`), and
  `initializeInstanceAndDevice()` calls `window_.createVulkanSurface(instance_, surface_)`
  (~line 123) to fill `VkSurfaceKHR surface_`. This is the **only** Vulkan-facing call that needs
  to change — swapchain/device/queue/VMA setup around it is untouched per the migration constraints.
- [src/ImGuiLayer.hpp](../../../src/ImGuiLayer.hpp) / [.cpp](../../../src/ImGuiLayer.cpp):
  `processEvent(const sf::Event&)` and `translateSfmlKeyToImGui(sf::Keyboard::Key)` — this whole
  file is being replaced by RmlUi rendering, but until that lands, note it as a consumer of the
  event loop shape (poll loop somewhere in `AppController.cpp`/`main.cpp` feeds it events).

## CMake swap
Follow the existing `FetchContent_Declare` pattern used for `imgui`/`spdlog`/`glm` in the root
[CMakeLists.txt](../../../CMakeLists.txt): remove the `SFML` block (`FETCHCONTENT_Declare(SFML ...)`
+ `SFML_BUILD_AUDIO`/`SFML_BUILD_GRAPHICS` cache vars + `FetchContent_MakeAvailable(SFML)`), add an
`SDL3` block pinned to a specific release tag. Before writing the exact `target_link_libraries`
name (e.g. `SDL3::SDL3`), confirm it against the vendored SDL3 `CMakeLists.txt` per the
[vendored-dependency-fact-check](../vendored-dependency-fact-check/SKILL.md) skill — SDL3's CMake
target names changed relative to SDL2 (no more `SDL2::SDL2main` special-casing by default).

## API mapping to verify against vendored SDL3 headers (do not assume from SDL2 knowledge)
| SFML 3.1 | SDL3 (verify exact signature before using) |
|---|---|
| `sf::Window` construction (title/size/style) | `SDL_CreateWindow` + `SDL_WindowFlags` (note: SDL3 dropped the `SDL_WINDOW_VULKAN` flag requirement subtleties — check current header) |
| `window.pollEvent()` / `sf::Event` variant loop | `SDL_PollEvent(&SDL_Event)` — SDL3's `SDL_Event` is a plain struct/union like SDL2, not a `std::variant` like SFML 3's `sf::Event` |
| `sf::Keyboard::Key`, `sf::Mouse::Button` | `SDL_Scancode`/`SDL_Keycode`, `SDL_BUTTON_*` — enum values differ from both SFML and SDL2 in places, re-check |
| `window.createVulkanSurface(instance, surface)` | `SDL_Vulkan_CreateSurface(window, instance, allocator, &surface)` — note the extra allocator param |
| `sf::Vulkan::getGraphicsRequiredInstanceExtensions()` | `SDL_Vulkan_GetInstanceExtensions(&count)` — returns `const char* const*`, different ownership/lifetime than SFML's `std::vector` |
| `window.setSize(...)` / resize handling | `SDL_SetWindowSize` + `SDL_EVENT_WINDOW_RESIZED` in the event loop |

## Procedure
1. Add the SDL3 `FetchContent` block; remove SFML's (only after audio is also migrated off SFML —
   see the [sdl3-audio-migration](../sdl3-audio-migration/SKILL.md) skill — since both currently
   come from the same `SFML` FetchContent target).
2. Swap `VulkanContext`'s stored window reference type and the single surface-creation call;
   rebuild and confirm the swapchain still initializes (per the mode's approach step 2) before
   touching any scene UI.
3. Replace the event poll loop and keyboard/mouse translation last, once RmlUi's SDL platform
   backend is wired up (it typically wants to own event translation into its own context — check
   RmlUi's `Backends/RmlUi_Platform_SDL.*` for the expected integration point rather than hand
   rolling input translation twice).
