# 0003: PlayLevel RmlUi contract and migration sequence

## Status

Accepted on 2026-09-05. This decision defines the boundary for migrating PlayLevel from the
legacy ImGui/Lua scene to RmlUi. It extends ADR 0002.

## Context

The legacy `assets/scenes/PlayLevel.lua` is more than a view. It polls raw engine state, decides
which overlays and actions are available, interprets keyboard and mouse input, formats gameplay
status, manages tower selection and placement, and contains several developer-only panels. That
makes gameplay policy difficult to test and couples the scene to ImGui's immediate-mode API.

PlayLevel is also the first migrated scene that needs the 3D renderer and RmlUi in the same frame.
The vendored RmlUi 6.3 SDL/Vulkan sample backend only exposes an initialization path that creates
and owns its Vulkan instance, device, swapchain, render pass, command buffers, and synchronization.
It cannot consume the existing `VulkanContext` handles. The current RmlUi executable therefore
cannot render `WorldRenderer` safely: the two renderers own unrelated Vulkan devices and
swapchains.

## Decision

### C++ owns UI policy

PlayLevel UI reads one immutable `PlayLevelUiSnapshot` per update and sends player intent through
`IPlayLevelUiApi`. The snapshot contains presentation-ready state: visibility, enabled state,
labels, progress, and disabled reasons. RML event handlers do not derive gameplay eligibility or
mutate simulation state directly.

The first contract lives in `src/rmlui/playlevel/PlayLevelUiContract.*`. It covers:

- loading, load failure, waiting, running, paused, victory, and defeat phases;
- base health, money, wave and enemy summaries;
- engine-decided start-wave availability and countdown presentation;
- tower loadout and placement presentation;
- selected tower upgrades, targeting, and selling;
- selected enemy information;
- explicit commands for start wave, slot selection, placement cancellation, selection clearing,
  upgrades, targeting, selling, pause, restart, and return to Lobby.

Lua remains appropriate for authored level and wave definitions. It is not the PlayLevel HUD
controller. The RmlUi PlayLevel scene will be a native event listener and snapshot renderer, like
the migrated Lobby.

### Renderer integration comes before scene activation

The RmlUi sample Vulkan backend is incompatible with the application's existing ownership model,
so the approved fallback from the RmlUi backend migration plan applies: adapt the RmlUi render
interface to record into the command buffer and render target owned by `VulkanContext`.
`WorldRenderer` and gameplay renderer internals remain unchanged.

The implementation order is:

1. Complete SDL3 ownership of the game window and Vulkan surface in `VulkanContext`.
2. Initialize RmlUi against that window and the existing Vulkan instance/device/swapchain.
3. Render a static RmlUi overlay after `WorldRenderer` in the same frame.
4. Adapt the legacy PlayLevel controllers to implement `IPlayLevelUiApi` and remove UI policy from
   the Lua render function.
5. Add the production PlayLevel RML/RCSS document and enable the Lobby-to-PlayLevel transition.
6. Remove the remaining PlayLevel ImGui bindings and regenerate Lua metadata.

Steps 1 and 2 are now represented by the standalone `NodeSpireVulkanContext` target and the
adapted `RenderInterface_VK`:
`VulkanContext` accepts the backend's `SDL_Window*`, enables SDL's required Vulkan instance
extensions, creates the one SDL Vulkan surface, and uses pixel dimensions for its swapchain. The
backend exposes its window through `Backend::GetWindow()`. RmlUi borrows the context's instance,
physical device, device, graphics queue, allocator, and target formats. Each frame it records into
the command buffer already opened by `VulkanContext`; only `VulkanContext` submits and presents.
RmlUi retains ownership of its shaders, pipelines, descriptors, geometry, textures, and upload
command pool.

Borrowing the application's command buffer also adopts its frame-fence lifetime. Each RmlUi draw
gets distinct uniform storage, and uniform allocations plus released geometry are deferred by the
`VulkanContext` frame index. `BeginFrame` reclaims only the bucket whose fence the application has
just waited. A geometry-level uniform allocation is not valid here because RmlUi can draw the same
compiled geometry multiple times with different transforms before Vulkan consumes the commands.

The shared depth target is `VK_FORMAT_D32_SFLOAT`, so it cannot support the sample backend's
stencil-based transformed clipping. The initial adapter uses axis-aligned Vulkan scissoring for
all clipping. This supports the production HUD documents; arbitrary transformed clip regions
remain deferred until there is a demonstrated need to add stencil to the shared render target.

Step 3 is implemented as a native `PlayLevelScene` shell. The active RmlUi scene owns an optional
world-render callback, and the application invokes it before `Rml::Context::Render()` in the same
dynamic-rendering command buffer. Lobby commits a structured launch configuration from
`assets/levels/catalog.json`; the PlayLevel shell loads and renders that map with `WorldRenderer`
and presents loading, failure/retry, and return-to-Lobby controls. Gameplay simulation, enemies,
towers, and wave commands remain intentionally disabled until step 4 connects the existing C++
controllers to `IPlayLevelUiApi`.

The native Lobby and PlayLevel share match lifecycle through `MultiplayerSession`. A host
broadcasts the selected catalog level before entering PlayLevel; clients consume that announcement
in the native Lobby update and transition automatically. Once every loaded scene reports ready,
only the host can send `PartyMatchBegin`; clients show "Waiting for host to start" and never receive
a start control. The active level descriptor remains in the session while a client returns to
Lobby, enabling Rejoin without reconnecting. A host returning to Lobby ends the party transport,
which returns connected clients to Lobby as soon as they observe the disconnect.

The Escape menu is a trim-and-rebuild of the legacy ImGui pause panel. It keeps live master,
music, and SFX volume controls, Resume, and Back to Lobby. Display mode, graphics quality, and
other settings that can recreate rendering resources remain exclusive to the main-menu Options
scene and are deliberately unavailable during a match.

Continuous PlayLevel camera controls read SDL3 keyboard and mouse state from the native scene
during `update()`. RmlUi remains the owner of SDL event translation for document interaction;
right-button mouse look temporarily enables window-relative mouse mode and releases it when the
button or scene is released. This avoids reintroducing the ImGui-dependent legacy camera input
path while preserving its movement behavior.

### Trim-and-rebuild scope

The production RmlUi HUD preserves match start, loading, wave countdown, economy, base health,
tower loadout and placement feedback, tower upgrades, targeting, selling, enemy inspection,
pause, victory, defeat, restart, and return-to-Lobby flows.

The Lua gameplay harness, model animation debugger, pick-sphere controls, placement-bound tuning,
raw renderer statistics, and manual damage/spend controls are not ported into the player HUD.
They are developer diagnostics and should move to a separate native diagnostics surface if they
are still needed. Their underlying engine capabilities are not removed by this decision.

## Consequences

- UI behavior can be unit-tested without RmlUi, Vulkan, Lua, or a loaded level.
- Multiplayer authority and validation remain in the game engine; the HUD receives command
  acceptance and rejection messages instead of duplicating rules.
- The shared world/UI frame is implemented but must be build- and runtime-validated before the
  gameplay controller migration begins.
- The existing one-process RmlUi Lua VM decision from ADR 0002 remains unchanged, but PlayLevel's
  production HUD does not require Lua scripting.