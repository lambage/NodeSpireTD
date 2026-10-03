#pragma once

namespace NodeSpireUi {

// Minimal app-lifecycle signal a Lua scene script can trigger via
// Scene.Quit() (see LuaSceneBindings). LambUiApp's main loop polls
// QuitRequested() once per frame alongside its own SDL_EVENT_QUIT handling.
void RequestQuit();
bool QuitRequested();

} // namespace NodeSpireUi
