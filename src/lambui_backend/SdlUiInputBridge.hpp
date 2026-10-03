#pragma once

// SDL3 -> LambUI::UIManager input bridge. The event-type/scancode mapping
// mirrors LambUI's own reference event pump verbatim
// (build/_deps/lambui-src/examples/sdl3/src/main.cpp), so this stays
// faithful to the library's own fact-checked conventions rather than
// reinventing the mapping.

#include <SDL3/SDL.h>

#include <functional>

namespace LambUI {
class UIManager;
}

namespace lambui_backend {

class SdlUiInputBridge {
  public:
    struct PumpResult {
        bool quitRequested = false;
        bool windowResized = false;
        int windowWidth = 0;
        int windowHeight = 0;
    };

    // Polls all pending SDL events for this frame and injects them into
    // uiManager. onKeyDown (if set) is invoked for every SDL_EVENT_KEY_DOWN
    // with both the raw SDL keycode (for global hotkeys like F5 UI reload or
    // F8 debugger toggle, which have no LambUI::ScanCode equivalent) and the
    // translated LambUI::ScanCode value (0 if the key has no mapping, e.g.
    // for routing to NodeSpireUi::SceneManager::handleKeyDown).
    static PumpResult Pump(LambUI::UIManager& uiManager,
                           const std::function<void(SDL_Keycode, uint32_t)>& onKeyDown = {});
};

} // namespace lambui_backend
