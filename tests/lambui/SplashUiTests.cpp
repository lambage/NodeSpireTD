#include <LambUI/LambUI.h>
#include <LambUILua/LuaBindings.h>
#include <gtest/gtest.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

class SplashRenderer : public LambUI::IRenderer {
  public:
    std::vector<LambUI::UIRenderCommand> commands;

    void SubmitRenderCommands(const std::vector<LambUI::UIRenderCommand>& bucket) override {
        commands = bucket;
    }
};

class SplashUiTest : public testing::Test {
  protected:
    std::unique_ptr<lua_State, decltype(&lua_close)> lua{luaL_newstate(), lua_close};
    std::shared_ptr<SplashRenderer> renderer = std::make_shared<SplashRenderer>();
    LambUI::UIManager manager{renderer};
    std::unique_ptr<LambUILua::LuaUIBindings> bindings;

    void SetUp() override {
        ASSERT_NE(lua, nullptr);
        luaL_openlibs(lua.get());
        bindings = std::make_unique<LambUILua::LuaUIBindings>(lua.get(), manager);
        manager.SetDisplaySize(1920, 1080);
    }

    void TearDown() override {
        manager.Clear();
        bindings.reset();
    }

    testing::AssertionResult Run(const char* script) {
        const int result = luaL_dostring(lua.get(), script);
        if (result == LUA_OK) {
            return testing::AssertionSuccess();
        }
        const std::string message = lua_tostring(lua.get(), -1);
        lua_pop(lua.get(), 1);
        return testing::AssertionFailure() << message;
    }

    testing::AssertionResult LoadScene(bool imageAvailable) {
        bindings->SetImageLoader([imageAvailable](const std::string& path) {
            EXPECT_EQ(path, "assets/images/splash_screen.png");
            return imageAvailable ? LambUI::UIImage{reinterpret_cast<void*>(uintptr_t{123}), 1376, 768}
                                  : LambUI::UIImage{};
        });
        if (!Run("Audio = { Play = function(...) end }")) {
            return testing::AssertionFailure() << "Unable to register scene dependencies";
        }
        const int result = luaL_dofile(lua.get(), NODESPIRE_SPLASH_SCRIPT);
        if (result == LUA_OK) {
            return testing::AssertionSuccess();
        }
        const std::string message = lua_tostring(lua.get(), -1);
        lua_pop(lua.get(), 1);
        return testing::AssertionFailure() << message;
    }
};

TEST_F(SplashUiTest, ImageIsRenderedAndFitsAfterResize) {
    ASSERT_TRUE(LoadScene(true));
    ASSERT_TRUE(Run("assert(OnUpdate == nil and Images == nil)"));
    for (const auto& viewport : std::vector<LambUI::UIRect>{{0, 0, 1920, 1080}, {0, 0, 800, 1200}, {0, 0, 2560, 1080}}) {
        manager.SetDisplaySize(viewport.width, viewport.height);
        manager.Update(0);
        manager.Render();

        const auto image = std::find_if(renderer->commands.begin(), renderer->commands.end(), [](const auto& command) {
            return command.textureHandle == reinterpret_cast<void*>(uintptr_t{123});
        });
        ASSERT_NE(image, renderer->commands.end());
        EXPECT_EQ(image->type, LambUI::RenderCommandType::DrawQuad);
        EXPECT_EQ(image->color, 0xFFFFFFFFu);
        const float scale = std::min(viewport.width / 1376.0f, viewport.height / 768.0f);
        EXPECT_NEAR(image->width, 1376 * scale, 0.01f);
        EXPECT_NEAR(image->height, 768 * scale, 0.01f);
        EXPECT_NEAR(image->x, (viewport.width - image->width) / 2, 0.01f);
        EXPECT_NEAR(image->y, (viewport.height - image->height) / 2, 0.01f);
    }
}

TEST_F(SplashUiTest, MainMenuPreloadsArtworkAndRoutesAllActions) {
    int loads = 0;
    const std::vector<std::string> sources = {
        "splash_screen", "play_button", "play_button_hover", "options_button",
        "options_button_hover", "quit_button", "quit_button_hover"
    };
    bindings->SetImageLoader([&](const std::string& path) {
        ++loads;
        for (size_t index = 0; index < sources.size(); ++index) {
            if (path == "assets/images/" + sources[index] + ".png")
                return LambUI::UIImage{reinterpret_cast<void*>(uintptr_t{100} + index), 1376, 768};
        }
        ADD_FAILURE() << "Unexpected image: " << path;
        return LambUI::UIImage{};
    });
    ASSERT_TRUE(Run(R"lua(
        Audio = { Preload = function(...) end, Play = function(...) end }
        Scene = {
            GoTo = function(scene) destination = scene end,
            Quit = function() quit = true end
        }
    )lua"));
    ASSERT_EQ(luaL_dofile(lua.get(), NODESPIRE_MAINMENU_SCRIPT), LUA_OK) << lua_tostring(lua.get(), -1);
    EXPECT_EQ(loads, 7);
    const std::vector<float> offsets = {-128, -6, 116};
    const std::vector<const char*> checks = {
        "assert(destination == 'Lobby')", "assert(destination == 'Options')", "assert(quit)"
    };
    for (const auto& viewport : std::vector<LambUI::UIRect>{{0, 0, 1920, 1080}, {0, 0, 800, 600}}) {
        manager.SetDisplaySize(viewport.width, viewport.height);
        manager.Update(0);
        for (size_t index = 0; index < offsets.size(); ++index) {
            ASSERT_TRUE(Run("destination, quit = nil, false"));
            manager.InjectMouseMove(viewport.width / 2, viewport.height / 2 + offsets[index]);
            manager.Update(0);
            manager.Render();
            const auto hoverTexture = reinterpret_cast<void*>(uintptr_t{102} + index * 2);
            EXPECT_TRUE(std::any_of(renderer->commands.begin(), renderer->commands.end(), [&](const auto& command) {
                return command.textureHandle == hoverTexture && command.color == 0xFFFFFFFFu;
            }));
            manager.InjectMouseButton(LambUI::MouseButton::Left, true);
            manager.InjectMouseButton(LambUI::MouseButton::Left, false);
            ASSERT_TRUE(Run(checks[index]));
            manager.InjectMouseMove(0, 0);
            manager.Update(0);
            manager.Render();
            const auto normalTexture = reinterpret_cast<void*>(uintptr_t{101} + index * 2);
            EXPECT_TRUE(std::any_of(renderer->commands.begin(), renderer->commands.end(), [&](const auto& command) {
                return command.textureHandle == normalTexture;
            }));
        }
    }
    EXPECT_EQ(loads, 7);
    EXPECT_EQ(lua_gettop(lua.get()), 0);
}

TEST_F(SplashUiTest, MainMenuMissingArtworkKeepsClickableTextButtons) {
    ASSERT_TRUE(Run(R"lua(
        Audio = { Preload = function(...) end, Play = function(...) end }
        Scene = { GoTo = function(scene) destination = scene end, Quit = function() end }
    )lua"));
    ASSERT_EQ(luaL_dofile(lua.get(), NODESPIRE_MAINMENU_SCRIPT), LUA_OK) << lua_tostring(lua.get(), -1);
    manager.Update(0);
    manager.Render();
    for (const auto& text : {"Play", "Options", "Exit"}) {
        EXPECT_TRUE(std::any_of(renderer->commands.begin(), renderer->commands.end(), [&](const auto& command) {
            return command.type == LambUI::RenderCommandType::DrawString && command.text == text;
        }));
    }
    manager.InjectMouseMove(960, 412);
    manager.InjectMouseButton(LambUI::MouseButton::Left, true);
    manager.InjectMouseButton(LambUI::MouseButton::Left, false);
    ASSERT_TRUE(Run("assert(destination == 'Lobby')"));
}

TEST_F(SplashUiTest, MissingImageKeepsTextFallback) {
    ASSERT_TRUE(LoadScene(false));
    manager.Update(0);
    manager.Render();
    EXPECT_EQ(lua_gettop(lua.get()), 0);
    EXPECT_TRUE(std::none_of(renderer->commands.begin(), renderer->commands.end(), [](const auto& command) {
        return command.textureHandle == reinterpret_cast<void*>(uintptr_t{123});
    }));
    EXPECT_TRUE(std::any_of(renderer->commands.begin(), renderer->commands.end(), [](const auto& command) {
        return command.type == LambUI::RenderCommandType::DrawString && command.text == "NodeSpire TD";
    }));
}

}