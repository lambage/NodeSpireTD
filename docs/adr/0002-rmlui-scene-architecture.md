# 0002: RmlUi Scene Architecture (replacing ImGui/SFML per-scene model)

## Status

Accepted on 2026-09-04. Covers the first slice of the RmlUi migration: the
`NodeSpireTD` executable's scene loading/handoff design and SplashScreen port.
Superseded/extended as later scenes (MainMenu, Options, Lobby, PlayLevel) land.

## Context

The legacy `NodeSpireTD-imgui` app (`src/AppController.*`, `src/Scenes.*`,
`src/scenes/IScene.hpp`) drives scenes with: one `lua_State*` per `IScene`
instance, an SFML event loop, and a per-frame ImGui immediate-mode `render()`
call driven by a Lua script per scene (`assets/scenes/*.lua`).

The new `NodeSpireTD` executable uses RmlUi's own SDL+Vulkan backend and
RmlUi's bundled Lua plugin (`RmlUi::Lua`). Investigating
`Source/Lua/LuaPlugin.cpp` in the vendored RmlUi source showed the plugin
holds its Lua state in a single static global (`static lua_State* g_L`) set
once when the plugin is registered (`Rml::Lua::Initialise()` /
`Rml::RegisterPlugin(new LuaPlugin(...))`) and torn down once at
`Rml::Shutdown()`. This is a process-wide singleton, not a handle you can swap
per scene without unregistering/re-registering the plugin (and re-running
`Factory::RegisterElementInstancer`/`RegisterEventListenerInstancer` globally)
on every scene transition.

## Decision

1. **One shared Lua VM for the whole RmlUi app's lifetime**, owned by RmlUi's
   Lua plugin itself (`Rml::Lua::Initialise()` with no external state). This
   replaces the legacy one-`lua_State`-per-scene model. If a scene later needs
   isolation from another scene's Lua globals, that will be done via
   per-script `_ENV` sandboxing (Lua 5.2+ upvalue mechanism) rather than
   separate `lua_State`s — not needed yet since SplashScene has no Lua at all
   (see below).
2. **New scene loading/handoff system under `src/rmlui/`**, independent of the
   legacy `src/scenes/`/`Scenes.hpp`/`IScene.hpp` (which stay untouched and
   keep building `NodeSpireTD-imgui` until each scene is fully migrated):
   - `rmlui/SceneTypes.hpp` — `SceneId` enum + `SceneTransition =
     std::optional<SceneId>`.
   - `rmlui/IScene.hpp` — `onEnter(Rml::Context&)`, `onExit(Rml::Context&)`,
     `update(float dt) -> SceneTransition`, optional `onKeyDown(...)`. No
     `render()`: RmlUi renders the active document(s) itself via
     `Context::Render()`, so scenes only own document lifecycle + transition
     logic.
   - `rmlui/SceneManager.hpp/.cpp` — owns exactly one live scene at a time;
     constructs the next scene and calls `onEnter` only after the previous
     scene's `onExit` runs.
   - `rmlui/scenes/*` — one `.hpp`/`.cpp` pair per scene, paired with a
     `.rml`/`.rcss` pair under `assets/ui/<scene>/`.
3. **RmlUi's own SDL+Vulkan backend is reused, but forked into our tree**
   (own `VkInstance`/`VkDevice`, not reconciled with `VulkanContext`). This is
   sufficient for scenes with no 3D world rendering (Splash, MainMenu,
   Options). Reconciliation with `VulkanContext` (per the
   `rmlui-sdl-vulkan-backend` skill) is deferred until a scene needs both
   RmlUi and `WorldRenderer` in the same frame (first hit expected at Lobby or
   PlayLevel) — tackling it now would be premature given neither scene needs
   3D content. See "Image format support" below for why it's forked rather
   than consumed via `add_subdirectory("Backends")`.

## Image format support in RmlUi's Vulkan backend

RmlUi's sample `RenderInterface_VK::LoadTexture` (the `SDL_VK` backend) only
accepts 24/32bit uncompressed TGA, rejecting every PNG under `assets/images/`
(including the splash screen image). Two approaches were considered:

- **Rejected: subclass `RenderInterface_VK`.** Infeasible as-is —
  `RmlUi_Backend_SDL_VK.cpp`'s `BackendData` holds `RenderInterface_VK
  render_interface;` as a concrete value member, not a pointer, so there is
  no virtual-dispatch seam without also forking the backend construction file.
- **Rejected: custom `Rml::FileInterface` transcoding to in-memory TGA.**
  Implemented briefly (`RmlUiImageFileInterface`, using `stb_image` to decode
  PNG/JPEG/BMP into a synthetic TGA buffer consumed unmodified by the
  vendored parser). Worked, but indirect/roundabout — rejected in favor of
  fixing the real problem directly.
- **Chosen: fork the `SDL_VK` backend combination into
  `src/rmlui_backend/`.** Copied `RmlUi_Platform_SDL.h/.cpp`,
  `RmlUi_Renderer_VK.h/.cpp`, and `RmlUi_Backend_SDL_VK.cpp` verbatim out of
  `Backends/`, then replaced `RenderInterface_VK::LoadTexture`'s hand-rolled
  TGA parser with `stb_image`'s `stbi_load_from_memory` (decodes PNG, JPEG,
  BMP, TGA, GIF, PSD, PIC, PNM), preserving the original's premultiplied-alpha
  conversion step. `src/CMakeLists.txt` now compiles these forked sources
  directly into `NodeSpireTD` instead of `add_subdirectory("Backends")` +
  linking `rmlui_backend_SDL_VK`. The large third-party payloads these files
  depend on but don't modify — `RmlUi_Include_Vulkan.h`,
  `RmlUi_Vulkan/vulkan.h` (GLAD-generated Vulkan loader), `vk_mem_alloc.h`,
  `ShadersCompiledSPV.h` (precompiled SPIR-V), and the generic `RmlUi_Backend.h`
  interface — are **not** duplicated into our tree; they're still resolved
  via `${rmlui_SOURCE_DIR}/Backends` on the include path, since quote-includes
  fall back to the include search path when the file isn't found next to the
  including file. Only the files we actually needed to change (or that tie
  directly to what we changed) were forked.

**Caveat carried over, not independently verified**: the premultiplied-alpha
conversion in the new `LoadTexture` (straight-alpha RGBA from `stb_image` ×
alpha, per channel) is copied as-is from the original TGA-only path's
behavior — it was never re-derived against the fragment shader
(`RmlUi_Vulkan/shader_frag_texture.frag` → `ShadersCompiledSPV.h`) to confirm
the compositing pipeline actually wants premultiplied input. It "worked" for
opaque/near-opaque PNGs in manual testing, but hasn't been stress-tested with
genuinely semi-transparent art. If soft-alpha edges (e.g. glow/shadow PNGs)
look wrong (dark fringing) later, revisit this conversion first.



## SplashScreen: what was ported vs. trimmed

- **Trimmed, not ported 1:1**: the legacy `SplashScreen.lua` did two things —
  laid out an ImGui window every frame (centered image + status text) and
  kicked off background music. The layout half is now fully replaced by
  declarative markup (`assets/ui/splash/splash.rml` + `.rcss`, flexbox
  centering + `position: absolute` status line) — there is **no Lua script
  for this scene at all**. All transition logic (minimum 3s duration, or
  instant skip on any keypress) lives in native `SplashScene::update()` /
  `onKeyDown()`.
- **Dropped for now, explicit follow-up**: the background music cue
  (`Audio.loadMusic("assets/music/Heroic_Demise.mp3")`). `AudioEngine` is
  still SFML-based and the `NodeSpireTD` target has no SFML dependency at all
  today; wiring it in now would mean either reintroducing SFML into the new
  target or front-running the SDL3 audio migration slice. Re-add once
  `sdl3-audio-migration` lands.
- **MainMenuScene** was added as a placeholder (static text, no buttons/Lua)
  purely to prove the Splash → MainMenu handoff end-to-end; the real MainMenu
  (GameButton-equivalent, Lua bindings) is a separate follow-up session.

## Follow-ups

- Design the RmlUi-idiomatic `GameButton`/`GameImageButton` replacement
  (RCSS `:hover`/`:active` states + `AudioEngine`-backed click/hover sfx via
  RmlUi event listeners) when MainMenu gets real buttons.
- Resolve SDL3 audio migration, then restore the splash music cue.
- Revisit Vulkan backend reconciliation once a scene needs `WorldRenderer` in
  the same frame as RmlUi.
