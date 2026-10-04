---
name: "Lua Rewrite"
description: "Use to optimize Lua UI scenes with a strict Model/View architecture."
tools: [read, edit, search, execute, todo]
user-invocable: true
disable-model-invocation: false
---

## Instructions
Adopt a strict Model/View architecture for Lua UI scenes:

1. State Contract (C++)
- Scene publishes immutable state snapshots with monotonically increasing revision.
- State publication occurs on state change only.
- State carries no layout instructions, only game data.

2. View Contract (Lua)
- `OnEnter()`: construct widget tree once.
- `OnUpdate(state, dt)`: apply declarative bindings only.
- `OnLayoutChanged(width, height)`: apply geometry/layout only.
- `OnExit()`: clean up any resources or state associated with the scene.

3. Command Contract (C++)
- Scene exposes a command surface for user actions (`Play.Start`, `Play.Upgrade`, etc.).
- Commands are invoked by the Lua view layer in response to user interactions.
- Commands do not mutate the view directly; they only affect the state, which is then published to Lua via `OnUpdate`.

## Architecture

### C++ side
- Keep scene-owned state revision/fingerprint mechanism.
- Add explicit viewport/layout publication path:
  - call Lua `OnLayoutChanged(width, height)` only when root size changes.
- Keep command surface for user actions (`Play.Start`, `Play.Upgrade`, etc.).
- Remove need for Lua `Play.State()` state comes from OnUpdate callback.
- Will use the 4 main lifecycle callbacks: `OnEnter()`, `OnUpdate(state)`, `OnLayoutChanged(width, height)`, and `OnExit()`.
- With respect to `OnUpdate(state)` the state is considered the single source of truth for game data, and any changes to the state should be reflected in the UI through this callback only.  The state should be a read-only object within the Lua view layer (which doesn't necessarily have to be enforced but changing it in Lua won't effect the actual state, for example setting 'state.gold = 9999999' won't actually give the player a gold boost).

### Lua side
Each scene script should define the following functions to comply with the Model/View architecture:
- `OnEnter()`: construct the widget tree once.
- `OnUpdate(state, dt)`: apply declarative bindings only.
- `OnLayoutChanged(width, height)`: apply geometry/layout only.
- `OnExit()`: clean up any resources or state associated with the scene.

Resources acquired in Lua need to be managed appropriately, objects created in c++ through the Lua interface should use proper weak_ptr semantics to avoid memory leaks and dangling references.

Interaction in the UI will call a structured API, for instance when a tower selection UI button is clicked, it will call `Play.SelectTower(towerId)`.

## Migration Plan

### Scene lifecycle protocol
- All scenes in C++:
  - publish `OnEnter()` when the scene is first entered.
  - publish `OnUpdate(state, dt)` every frame.
  - publish `OnLayoutChanged(width,height)` on viewport change.
  - publish `OnExit()` when the scene is about to be destroyed.
- All scenes in Lua:
  - setup widget tree in `OnEnter()` and any other initialization logic required for the scene. (i.e. preload audio and image assets as needed)
  - apply UI updates in `OnUpdate(state, dt)`.
  - apply geometry/layout in `OnLayoutChanged(width, height)`.
  - clean up resources and any other teardown logic in `OnExit()`.

## Mitigations
- Add verbose debug mode (`NODESPIRE_UI_TRACE=1`).

## End Goal
- Scene scripts become shorter and declarative.
