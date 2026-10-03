#include "lambui/LuaUiScene.hpp"

#include "lambui/LuaAudioBindings.hpp"
#include "lambui/LuaSceneBindings.hpp"
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

void LuaUiScene::onEnter(LambUI::UIManager& ui, AudioEngine& audio) {
    pendingTransition_.reset();
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
            if (lua_pcall(lua_, 0, 0, 0) != LUA_OK) {
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
}

SceneTransition LuaUiScene::update(float dt) {
    if (lua_) {
        lua_getglobal(lua_, "OnUpdate");
        if (lua_isfunction(lua_, -1)) {
            lua_pushnumber(lua_, dt);
            if (lua_pcall(lua_, 1, 0, 0) != LUA_OK) {
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
