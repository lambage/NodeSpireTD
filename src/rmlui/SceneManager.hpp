#pragma once

#include "rmlui/IScene.hpp"
#include "rmlui/SceneTypes.hpp"

#include <RmlUi/Core/Input.h>
#include <memory>

namespace Rml {
class Context;
}

namespace NodeSpireUi {

// Owns the currently active scene and switches between scenes on request.
// Only one scene (and its RmlUi document) exists at a time; scenes are
// constructed lazily on entry and destroyed on exit.
class SceneManager {
  public:
    SceneManager(Rml::Context& context, SceneId initialScene);

    void update(float dt);
    void handleKeyDown(Rml::Input::KeyIdentifier key);

    SceneId activeSceneId() const { return activeSceneId_; }

  private:
    void enterScene(SceneId id);
    void applyTransition(const SceneTransition& transition);

    Rml::Context& context_;
    SceneId activeSceneId_;
    std::unique_ptr<IScene> activeScene_;
};

} // namespace NodeSpireUi
