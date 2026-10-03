# NodeSpireTD

Cross-platform C++20 tower defense project using SDL3, RmlUi, and a Vulkan-first renderer.

## Goals

- Keep development workflow consistent between Windows and Linux.
- Build the `NodeSpireTD` game and its multiplayer/RmlUi test suite.

## Prerequisites

- CMake >= 3.24
- Git
- Git LFS (required for fonts, images, audio, and models)
- Ninja
- C++ toolchain:
	- Windows: Visual Studio 2022 Build Tools (MSVC). Run the Ninja configure and build commands
	  from **Developer PowerShell for VS 2022** so MSVC's standard-library include paths are set.
	- Linux: GCC or Clang

After syncing the repository, download its LFS assets from the NodeSpireTD directory:

```bash
git lfs pull
```

For repo-tool checkouts whose remote is named `github`, use `git lfs pull github`.
Run this before installing the game. A checkout with LFS downloads skipped contains
small text pointers instead of usable assets; fonts are typically hundreds of KB,
not roughly 130 bytes. After fetching assets, rerun `cmake --install build`.

### Option 1: Through CMake target

Configure your project once:

```bash
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug
```

Then run:

```bash
cmake --build build
```

Run from the build tree or install it first:

- Windows: `build/src/NodeSpireTD.exe`
- Install: `cmake --install build`, then run `build/install/NodeSpireTD.exe`

The default log level is `info`; `-v`/`--verbose` enables debug and
`--extra-verbose` enables trace. Logs rotate at `logs/nodespiretd.log` (5 MB x 3 files).

## Tower Talent UI

Each tower owns its talent presentation in a separate Lua module:
[ArcherHut.lua](assets/scenes/towers/ArcherHut.lua) and
[MageTower.lua](assets/scenes/towers/MageTower.lua). Their positions, connections,
and artwork are authored explicitly, not inferred from the gameplay tree.

To add a tower UI, register its archetype in `towerViews` in
[PlayLevel.lua](assets/scenes/PlayLevel.lua). A module returns a factory accepting
`(context, parent)` and creates its widgets under `parent`. The returned object
implements `Update(selection, availableWidth)` and returns its content width and
height. Instances are reused; updates must refresh controls from the current
selection. The host manages visibility, scrolling, the draggable window, and the
shared biography, stats, and sell action. The loadout remains available while inspecting.

`context` supplies namespaced `frame`, `text`, and `button` helpers, `place`,
`colors`, and `purchase(talentId)`. Purchases are checked against the current
selection, ownership, pause state, and talent availability. The optional
[TalentControls.lua](assets/scenes/towers/TalentControls.lua) helper provides
`Begin(selection)`, `Node(id, left, top, artwork)`, and `Link(parentId, childId)`.
A tower can instead build entirely different controls with the Lua UI API.
Gameplay requirements and costs stay in the tower definition; update its UI's
authored links when those requirements change. An unregistered tower shows an
explicit unavailable message instead of generating a generic talent tree.

## MCP: Live Gameplay Settings

The repository includes an MCP server at `tools/mcp_game_settings_server.py` for
analyzing and tweaking `config/settings.json` and host-side gameplay tuning values.

- `settings_get`: Returns current settings.
- `settings_analyze`: Returns derived audio gain and quality warnings.
- `settings_patch`: Applies a partial settings patch.
- `settings_reset_defaults`: Restores default values.
- `gameplay_tuning_status`: Shows gameplay tuning status and current values.
- `gameplay_tuning_set_enabled`: Turns gameplay tuning on/off in `config/devtools.json`.
- `gameplay_tuning_patch`: Applies a partial patch to `config/gameplay_tuning.json`.
- `gameplay_tuning_reset`: Restores gameplay tuning defaults.

Run it as a stdio MCP server:

```bash
python tools/mcp_game_settings_server.py
```

`PlayLevelScene` polls the settings file and applies external audio changes at runtime,
so MCP updates to `masterVolume`, `musicVolume`, and `sfxVolume` take effect during a match.

Gameplay tuning anti-cheat gates are intentionally layered:

1. Build-time gate: gameplay tuning hooks are compiled only when
	`-DNODESPIRE_ENABLE_GAMEPLAY_MCP_TUNING=ON` is set.
2. Runtime gate: `config/devtools.json` must contain
	`"enableGameplayMcpTuning": true`.
3. MCP write lock: gameplay tuning writes are rejected unless the MCP server process
	is launched with environment variable `NODESPIRE_ENABLE_DEV_MCP_TUNING=1`.

This keeps gameplay tuning off by default for live builds/runs while preserving a
simple opt-in path for local dev and balancing sessions.


# Known Issues

- no chat/whisper feature, would be nice to have a WoW style command prompt/chat
- would rather have a party system than the current host starts map and client connects
- should probably just pick a port number and not allow users to modify it, may also need some additional data in the initial handshake so we can reject clients trying to connect to that port on a machine that might be running a different service and not get cross talk

