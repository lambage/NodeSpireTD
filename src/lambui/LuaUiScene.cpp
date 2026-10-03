#include "lambui/LuaUiScene.hpp"

#include "lambui/LuaAudioBindings.hpp"
#include "lambui/LuaSceneBindings.hpp"
#include "lambui/UiTrace.hpp"
#include "lambui_backend/VulkanUiRenderer.hpp"
#include "lua.hpp"

#include <LambUI/UIManager.h>
#include <LambUI/UIWidget.h>
#include <LambUILua/LuaBindings.h>

#include <cstdio>
#include <exception>

namespace NodeSpireUi {

LuaUiScene::LuaUiScene(std::string scriptPath, lambui_backend::VulkanUiRenderer& renderer)
    : scriptPath_(std::move(scriptPath)), renderer_(renderer) {}

LuaUiScene::~LuaUiScene() = default;

void LuaUiScene::pushOnUpdateState(lua_State* lua) {
    lua_pushnil(lua);
}

void LuaUiScene::pushOnEnterState(lua_State* lua) {
    pushOnUpdateState(lua);
}

void LuaUiScene::onEnter(LambUI::UIManager& ui, AudioEngine& audio, float viewportWidth, float viewportHeight) {
    UiTrace("LuaUiScene[%s]: OnEnter(%g, %g)", scriptPath_.c_str(), viewportWidth, viewportHeight);
    pendingTransition_.reset();
    layoutWidth_ = viewportWidth;
    layoutHeight_ = viewportHeight;
    ui_ = &ui;
    ui.Clear();

    lua_ = luaL_newstate();
    luaL_openlibs(lua_);
    bindings_ = std::make_unique<LambUILua::LuaUIBindings>(lua_, ui);
    bindings_->SetFontResolver([renderer = &renderer_](const std::string& name, int size) {
        return renderer->GetFontHandle(name, size);
    });
    BindAudioEngine(lua_, audio);
    BindSceneControl(lua_, *this);
    bindings_->SetImageLoader([renderer = &renderer_](const std::string& path) -> LambUI::UIImage {
        try {
            const auto image = renderer->LoadImage(path);
            return {image.handle, image.width, image.height};
        } catch (const std::exception& error) {
            std::fprintf(stderr, "LambUI image load failed for '%s': %s\n", path.c_str(), error.what());
        }
        return {};
    });

    bindSceneApi(lua_, audio);
    if (luaL_dofile(lua_, scriptPath_.c_str()) != LUA_OK) {
        const char* error = lua_tostring(lua_, -1);
        std::fprintf(stderr, "LuaUiScene: failed to run '%s': %s\n", scriptPath_.c_str(),
                     error ? error : "unknown error");
        lua_pop(lua_, 1);
    }

    if (lua_) {
        lua_getglobal(lua_, "OnEnter");
        if (lua_isfunction(lua_, -1)) {
            lua_pushnumber(lua_, viewportWidth);
            lua_pushnumber(lua_, viewportHeight);
            pushOnEnterState(lua_);
            if (lua_pcall(lua_, 3, 0, 0) != LUA_OK) {
                const char* error = lua_tostring(lua_, -1);
                std::fprintf(stderr, "LuaUiScene: OnEnter failed: %s\n", error ? error : "unknown error");
                lua_pop(lua_, 1);
            }
        } else {
            lua_pop(lua_, 1);
        }
    }

    onSceneEnter(audio);
}

void LuaUiScene::onExit(LambUI::UIManager& ui) {
    UiTrace("LuaUiScene[%s]: OnExit", scriptPath_.c_str());
    if (lua_) {
        lua_getglobal(lua_, "OnExit");
        if (lua_isfunction(lua_, -1)) {
            if (lua_pcall(lua_, 0, 0, 0) != LUA_OK) {
                const char* error = lua_tostring(lua_, -1);
                std::fprintf(stderr, "LuaUiScene: OnExit failed: %s\n", error ? error : "unknown error");
                lua_pop(lua_, 1);
            }
        } else {
            lua_pop(lua_, 1);
        }
    }
    ui.Clear();
    bindings_.reset();
    if (lua_) {
        lua_close(lua_);
        lua_ = nullptr;
    }
    layoutWidth_ = -1.0f;
    layoutHeight_ = -1.0f;
    ui_ = nullptr;
}

SceneTransition LuaUiScene::update(float dt) {
    if (lua_) {
        lua_getglobal(lua_, "OnUpdate");
        if (lua_isfunction(lua_, -1)) {
            pushOnUpdateState(lua_);
            lua_pushnumber(lua_, dt);
            if (lua_pcall(lua_, 2, 0, 0) != LUA_OK) {
                const char* error = lua_tostring(lua_, -1);
                std::fprintf(stderr, "LuaUiScene: OnUpdate failed: %s\n", error ? error : "unknown error");
                lua_pop(lua_, 1);
            }
        } else {
            lua_pop(lua_, 1);
        }
    }
    onUpdateScene(dt);
    return pendingTransition_;
}

void LuaUiScene::onLayoutChanged(float width, float height) {
    if (!lua_ || (width == layoutWidth_ && height == layoutHeight_)) {
        return;
    }
    UiTrace("LuaUiScene[%s]: OnLayoutChanged(%g, %g)", scriptPath_.c_str(), width, height);
    layoutWidth_ = width;
    layoutHeight_ = height;
    lua_getglobal(lua_, "OnLayoutChanged");
    if (!lua_isfunction(lua_, -1)) {
        lua_pop(lua_, 1);
        return;
    }
    lua_pushnumber(lua_, width);
    lua_pushnumber(lua_, height);
    if (lua_pcall(lua_, 2, 0, 0) != LUA_OK) {
        const char* error = lua_tostring(lua_, -1);
        std::fprintf(stderr, "LuaUiScene: OnLayoutChanged failed: %s\n", error ? error : "unknown error");
        lua_pop(lua_, 1);
    }
}

bool LuaUiScene::handleShortcut(uint32_t scanCode) {
    if (lua_) {
        lua_getglobal(lua_, "OnShortcut");
        if (lua_isfunction(lua_, -1)) {
            lua_pushinteger(lua_, static_cast<lua_Integer>(scanCode));
            if (lua_pcall(lua_, 1, 1, 0) == LUA_OK) {
                const bool handled = lua_toboolean(lua_, -1) != 0;
                lua_pop(lua_, 1);
                if (handled) {
                    return true;
                }
            } else {
                const char* error = lua_tostring(lua_, -1);
                std::fprintf(stderr, "LuaUiScene: OnShortcut failed: %s\n", error ? error : "unknown error");
                lua_pop(lua_, 1);
            }
        } else {
            lua_pop(lua_, 1);
        }
    }
    return handleSceneShortcut(scanCode);
}

} // namespace NodeSpireUi
