---
name: rmlui-sdl-vulkan-backend
description: 'Use when bringing up RmlUi''s native SDL+Vulkan backend in NodeSpireTD and reconciling it with the existing hand-rolled VulkanContext (instance/device/swapchain/queue/descriptor pool owned by us, built with volk + vk-bootstrap + VMA). Covers locating RmlUi''s Backends/ source once vendored, the integration seam between Rml::RenderInterface/SystemInterface and our VulkanContext, and when to fall back to a custom RenderInterface.'
---

# RmlUi Native SDL+Vulkan Backend Integration

## Why this is the tricky part of the migration
RmlUi's official `Backends/` samples (e.g. `RmlUi_Platform_SDL.cpp` + `RmlUi_Renderer_VK.cpp`) are
written to **own** the whole stack — they create their own `VkInstance`/`VkDevice`/swapchain via
their own bootstrap. NodeSpireTD already owns all of that in
[src/VulkanContext.hpp](../../../src/VulkanContext.hpp)/.cpp (volk + vk-bootstrap + VMA,
constructed from the window, exposing `instance()`, `physicalDevice()`, `device()`,
`graphicsQueue()`, `graphicsQueueFamily()`, `descriptorPool()`, `allocator()`, `commandPool()`).
Per the mode's migration decision, WorldRenderer/VulkanContext internals must not change — so the
integration goal is: **extract just the `Rml::RenderInterface` implementation** from RmlUi's
Vulkan backend and feed it our already-created Vulkan objects, rather than letting RmlUi bootstrap
its own device.

## Procedure
1. **Locate and read the real backend source before assuming its API**, per the
   [vendored-dependency-fact-check](../vendored-dependency-fact-check/SKILL.md) skill. Once RmlUi
   is added to `FetchContent`, look under `build/_deps/rmlui-src/Backends/` for files matching
   `RmlUi_Renderer_VK.*` and `RmlUi_Platform_SDL.*`. Check RmlUi's `Backends/CMakeLists.txt` (or
   `Backends/README`) for which backend combination target name to link (RmlUi ships several
   backend combinations behind CMake options — confirm the exact option/target name rather than
   guessing `RmlUi_Backend_SDL_VK` or similar).
2. **Check whether `RenderInterface_VK` (or equivalent) has a constructor/init path that accepts
   externally-owned Vulkan handles** (instance, physical device, device, queue + family, a
   descriptor pool, command pool). This is the load-bearing fact to confirm — do not assume it
   exists; if the vendored backend only supports self-created devices, that's when the mode's
   fallback applies: **write a small custom `Rml::RenderInterface`** that submits into our existing
   `VulkanContext` command buffers (reusing `beginFrameRecording`/`endFrameRecordingAndSubmit`),
   modeled on how [ImGuiLayer](../../../src/ImGuiLayer.hpp) already does this today for Dear ImGui
   (`initializeVulkanBackend(const VulkanContext&)`, `renderDrawData(VkCommandBuffer)`).
3. **Surface creation**: once SDL3 windowing lands (see
   [sdl3-windowing-migration](../sdl3-windowing-migration/SKILL.md)), RmlUi's SDL platform backend
   may also want to query/create its own surface or extensions list — make sure only one code path
   (ours, in `VulkanContext::initializeInstanceAndDevice`) actually calls
   `SDL_Vulkan_CreateSurface`, and RmlUi's backend is pointed at the resulting `VkSurfaceKHR`/
   `VkDevice` rather than creating a second one.
4. **System/font/file interfaces**: RmlUi also needs `Rml::SystemInterface` (timing) and file
   loading; check whether the SDL backend's system interface is reusable standalone (it usually
   is, being windowing-only) even if the renderer interface is swapped for a custom one.
5. **Bring-up order**: get a minimal RmlUi document (a static `.rml`/`.rcss` pair, no Lua yet)
   rendering inside the existing frame loop before touching any scene — this isolates renderer
   integration bugs from Lua/scene-binding bugs. Only after that works, move to
   [rmlui-lua-scene-integration](../rmlui-lua-scene-integration/SKILL.md).

## Things to explicitly ask the user about if unclear
- Which RmlUi CMake backend option/target actually exists for SDL3 + Vulkan in the pinned version
  (RmlUi's SDL/Vulkan backend support may be tied to a specific release — confirm the `GIT_TAG` you
  plan to pin supports it before writing integration code around it).
- Whether the fallback custom `RenderInterface` is warranted (per the mode's constraint: only fall
  back if the built-in backend proves incompatible with `VulkanContext`'s ownership model — say so
  explicitly if you hit that point rather than silently switching approaches).
