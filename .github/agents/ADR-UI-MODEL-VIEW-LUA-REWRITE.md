# ADR: Lua UI Model/View Rewrite (RmlUi-style Data Binding)

## Status
Proposed

## Context
The current LambUI + Lua flow still performs substantial imperative UI mutation from scene scripts.
Even with revision-gated updates, scripts still contain view orchestration that should belong to a declarative view model.

RmlUi worked well because:
1. C++ owned authoritative state.
2. Markup/view described structure and binding targets.
3. Runtime applied changed values without per-frame script-driven widget orchestration.

We want the same outcome for Lua scenes:
- Lua creates view structure once.
- C++ publishes state deltas/revisions.
- Binding runtime applies only changed fields.
- Layout runs only when viewport/layout constraints change, not on normal state changes.

## Decision
Adopt a strict Model/View architecture for Lua UI scenes with three contracts:

1. State Contract (C++)
- Scene publishes immutable state snapshots with monotonically increasing revision.
- State publication occurs on state change only.
- State carries no layout instructions, only domain/view data.

2. View Contract (Lua)
- `OnEnter`: construct widget tree once.
- `OnStateChanged(state)`: apply declarative bindings only.
- `OnLayoutChanged(width, height)`: apply geometry/layout only.
- `OnUpdate`: no UI mutation logic (profiling-only or empty).

3. Binding Runtime Contract (Lua)
- Path-based watchers: `state.path -> setter(widget, value)`.
- Diff cache per binding key.
- Collection binding support with keyed child reuse.
- No imperative refresh loop in scene scripts.

## Architecture

### C++ side
- Keep scene-owned state revision/fingerprint mechanism.
- Add explicit viewport/layout publication path:
  - call Lua `OnLayoutChanged(width, height)` only when root size changes.
- Keep command surface for user actions (`Play.Start`, `Play.Upgrade`, etc.).
- Remove need for Lua `Play.State()` pull in steady state; push only.

### Lua side
Each scene script is split into three sections:

1. `buildView()`
- Creates all persistent controls.
- Registers binding rules.
- Registers collection/item templates.

2. `defineBindings()`
- Scalar bindings: text/visible/color/enabled/value.
- Collection bindings: slots, upgrades, chat rows.
- Computed bindings can derive values from full state but remain pure.

3. `defineLayout()`
- Root-level regions and anchors.
- Runs only on `OnLayoutChanged` and explicit local layout-invalidations.

## Migration Plan

### Phase 1: Runtime foundation
- Extend `assets/scenes/UiBindings.lua` with:
  - `watch(path, applyFn)`
  - `watchComputed(key, computeFn, applyFn)`
  - `watchList(path, keyFn, createFn, updateFn, destroyFn)`
  - `apply(state)` with diff cache
- Add a separate layout registry:
  - `defineLayout(fn)` and `applyLayout(width, height)`

### Phase 2: Scene lifecycle protocol
- In PlayLevel scene C++:
  - publish `OnStateChanged(state)` on revision change.
  - publish `OnLayoutChanged(width,height)` on viewport change.
- In Lua PlayLevel:
  - delete refresh loop behavior from `OnUpdate`.
  - move all `place(...)` calls to `OnLayoutChanged` path.

### Phase 3: PlayLevel binding migration
- Migrate scalar HUD/status/pause fields to bindings.
- Migrate slot rows to collection binding with item reuse.
- Migrate profile/upgrades to collection binding with keyed nodes/links.
- Remove residual imperative per-state mutation logic.

### Phase 4: Rollout to other scenes
- Lobby, Options, MainMenu.
- Enforce pattern through template and lint-style checks.

## Acceptance Criteria
1. No scene script mutates UI geometry from `OnUpdate`.
2. `OnUpdate` does not call any setter APIs except optional debug/profiler hooks.
3. State updates require no explicit scene `refresh` function.
4. Layout recomputation occurs only on viewport/layout invalidation.
5. Profiling with static screen shows near-zero Lua UI work except on events.

## Risks
- Collection bindings for dynamic trees (talents) need careful keyed reuse logic.
- Incorrect diff granularity can cause stale values or extra updates.

## Mitigations
- Add verbose binding debug mode (`NODESPIRE_UI_BIND_TRACE=1`).
- Add integration tests for event-driven updates and resize behavior.

## Consequences
- Scene scripts become shorter and declarative.
- UI update cost scales with changed state, not frame rate.
- Architecture matches prior successful RmlUi data-binding model while retaining LambUI.
