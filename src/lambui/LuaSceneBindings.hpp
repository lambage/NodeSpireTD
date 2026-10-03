#pragma once

struct lua_State;

namespace NodeSpireUi {

class LuaUiScene;

// Exposes a global "Scene" table to the given Lua state:
//   Scene.GoTo(name)  -- name: "Splash" | "MainMenu" | "Lobby" | "Options" | "PlayLevel"
//   Scene.Quit()      -- requests the app close (see AppControl::RequestQuit)
// so a scene's own script can drive its own transitions (e.g. a MainMenu
// button's OnClick calling Scene.GoTo("Lobby")) without any native
// per-button C++ glue.
void BindSceneControl(lua_State* L, LuaUiScene& scene);

} // namespace NodeSpireUi
