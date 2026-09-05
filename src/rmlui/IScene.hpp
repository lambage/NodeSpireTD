#pragma once

#include "rmlui/SceneTypes.hpp"

#include <RmlUi/Core/Input.h>

class AudioEngine;

namespace Rml {
class Context;
}

namespace NodeSpireUi {

// A screen driven entirely by RmlUi documents/events, replacing the legacy
// IScene's per-frame ImGui immediate-mode render() call. Implementations load
// their .rml document in onEnter() and close/unload it in onExit(); RmlUi
// itself handles rendering the active document(s) each frame via
// Rml::Context::Render(), so scenes don't need a render() method at all.
class IScene {
  public:
    virtual ~IScene() = default;

    virtual void onEnter(Rml::Context& context, AudioEngine& audio) = 0;
    virtual void onExit(Rml::Context& context) = 0;

    // Called once per frame before context.Update(). Return a SceneId to
    // request a transition.
    virtual SceneTransition update(float dt) = 0;

    // Called for every key press while this scene is active. Return a
    // SceneId to request a transition; return std::nullopt to leave the key
    // unhandled (e.g. so the app-level F8 debugger toggle still applies).
    virtual SceneTransition onKeyDown(Rml::Input::KeyIdentifier /*key*/) {
        return std::nullopt;
    }

    // Called after RmlUi leaves a key unhandled. Return true when the active scene consumes it.
    virtual bool handleShortcut(Rml::Input::KeyIdentifier /*key*/) { return false; }
};

} // namespace NodeSpireUi
