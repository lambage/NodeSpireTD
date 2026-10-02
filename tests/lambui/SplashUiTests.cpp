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