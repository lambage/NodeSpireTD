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
#include <filesystem>
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

class LobbyUiTest : public SplashUiTest {
  protected:
    int imageLoads = 0;

    void SetUp() override {
        SplashUiTest::SetUp();
        bindings->SetImageLoader([&](const std::string&) {
            ++imageLoads;
            return LambUI::UIImage{reinterpret_cast<void*>(uintptr_t{123}), 640, 360};
        });
        bindings->SetFontResolver([](const std::string&, int) -> void* {
            return reinterpret_cast<void*>(uintptr_t{456});
        });
        ASSERT_TRUE(Run(R"lua(
        Audio = { Preload = function(...) end, Play = function(...) end }
        Scene = { GoTo = function(scene) destination = scene end }
        frames = {}
        local create = UI.CreateFrame
        UI.CreateFrame = function(kind, name, parent)
            local frame = create(kind, name, parent)
            frames[name] = frame
            return frame
        end
        snapshot = {
            role = 'Solo', displayName = 'Player', members = {}, chat = {},
            levels = {
                {id = 'test', name = 'Test level', description = 'A test mission', waves = '10', thumbnail = 'test.png'},
                {id = 'second', name = 'Second level', description = 'Another mission', waves = '25', thumbnail = 'second.png'}
            },
            towers = {{id = 'arrow', name = 'Arrow', selected = true, cost = 150, portrait = 'tower.png'}},
            selectedLevel = 1, canStart = true, ready = false, activeMatch = false, status = ''
        }
        Lobby = {
            State = function() return snapshot end,
            Host = function(name)
                hostedName = name
                snapshot.role, snapshot.canStart = 'Host', false
                snapshot.members = {{id = 1, name = name, localPlayer = true, host = true, ready = false}}
                return true
            end,
            Join = function(name, address) joinedName, joinedAddress = name, address; return false, 'Connection failed' end,
            Leave = function() snapshot.role, snapshot.members, snapshot.chat = 'Solo', {}, {}; return true end,
            Ready = function(value) snapshot.ready, snapshot.canStart = value, value; snapshot.members[1].ready = value; return true end,
            SendChat = function(value) sentChat = value; snapshot.chat = {'Player: ' .. value}; return true end,
            Kick = function(id) kickedId = id; return true end,
            SelectLevel = function(index) selectedIndex = index; snapshot.selectedLevel = index; return true end,
            ToggleTower = function(id) toggledId = id; return false, 'Loadout full' end,
            Start = function() started = true; return true end
        }
        )lua"));
        const auto script = std::filesystem::path(NODESPIRE_MAINMENU_SCRIPT).parent_path() / "Lobby.lua";
        ASSERT_EQ(luaL_dofile(lua.get(), script.string().c_str()), LUA_OK) << lua_tostring(lua.get(), -1);
        Layout(1920, 1080);
    }

    void Refresh() {
        ASSERT_TRUE(Run("OnUpdate(1)"));
        manager.Update(0);
        manager.Render();
    }

    void Layout(float width, float height) {
        manager.SetDisplaySize(width, height);
        manager.Update(0);
        Refresh();
    }

    void Click(const char* name) {
        const auto script = std::string("local left, top, width, height = frames.") + name +
            ":GetRect(); clickX, clickY = left + width / 2, top + height / 2";
        ASSERT_TRUE(Run(script.c_str()));
        lua_getglobal(lua.get(), "clickX");
        lua_getglobal(lua.get(), "clickY");
        manager.InjectMouseMove(static_cast<float>(lua_tonumber(lua.get(), -2)), static_cast<float>(lua_tonumber(lua.get(), -1)));
        lua_pop(lua.get(), 2);
        manager.InjectMouseButton(LambUI::MouseButton::Left, true);
        manager.InjectMouseButton(LambUI::MouseButton::Left, false);
        Refresh();
    }

    bool HasText(const std::string& text) {
        return std::any_of(renderer->commands.begin(), renderer->commands.end(), [&](const auto& command) {
            return command.type == LambUI::RenderCommandType::DrawString && command.text == text;
        });
    }
};

TEST_F(LobbyUiTest, RendersCoreControls) {
    for (const auto& text : {"Match", "Start solo", "Create party", "Join", "Choose your defenses", "Test level"}) {
        EXPECT_TRUE(HasText(text)) << text;
    }
    EXPECT_EQ(lua_gettop(lua.get()), 0);
}

TEST_F(LobbyUiTest, HostsReadiesChatsAndLeaves) {
    Click("HostButton");
    ASSERT_TRUE(Run("assert(hostedName == 'Player' and frames.ActiveParty:IsVisible() and not frames.PartySetup:IsVisible())"));
    Click("StartButton");
    ASSERT_TRUE(Run("assert(started == nil)"));
    Click("ReadyToggle");
    ASSERT_TRUE(Run("assert(snapshot.ready and snapshot.canStart)"));
    ASSERT_TRUE(Run("frames.ChatInput:SetText('Hello party')"));
    Click("SendButton");
    ASSERT_TRUE(Run("assert(sentChat == 'Hello party' and frames.ChatInput:GetText() == '')"));
    EXPECT_TRUE(HasText("Player: Hello party"));
    Click("StartButton");
    ASSERT_TRUE(Run("assert(started)"));
    Click("LeaveButton");
    ASSERT_TRUE(Run("assert(frames.PartySetup:IsVisible() and not frames.ActiveParty:IsVisible())"));
}

TEST_F(LobbyUiTest, FailedJoinAndLoadoutChangeKeepInputsAndAuthoritativeSelection) {
    ASSERT_TRUE(Run("frames.PlayerName:SetText('Test name'); frames.JoinAddress:SetText('192.0.2.1')"));
    Click("JoinButton");
    ASSERT_TRUE(Run("assert(joinedName == 'Test name' and joinedAddress == '192.0.2.1')"));
    EXPECT_TRUE(HasText("Connection failed"));
    Click("Tower1");
    ASSERT_TRUE(Run("assert(toggledId == 'arrow' and snapshot.towers[1].selected)"));
    EXPECT_TRUE(HasText("Loadout full"));
}

TEST_F(LobbyUiTest, ClientCannotStartOrKickButCanRejoinAnActiveMatch) {
    ASSERT_TRUE(Run(R"lua(
        snapshot.role, snapshot.canStart = 'Client', false
        snapshot.members = {{id = 7, name = 'Leader', host = true}, {id = 8, name = 'Player', localPlayer = true}}
    )lua"));
    Refresh();
    EXPECT_TRUE(HasText("Awaiting host"));
    ASSERT_TRUE(Run("assert(not frames.Kick1:IsVisible() and not frames.Kick2:IsVisible())"));
    Click("StartButton");
    ASSERT_TRUE(Run("assert(started == nil)"));
    ASSERT_TRUE(Run("snapshot.activeMatch, snapshot.canStart = true, true"));
    Refresh();
    EXPECT_TRUE(HasText("Rejoin match"));
    Click("StartButton");
    ASSERT_TRUE(Run("assert(started)"));
}

TEST_F(LobbyUiTest, FooterStaysInsideSmallAndWideWindowsAndBackRoutesToMenu) {
    for (const auto& viewport : std::vector<LambUI::UIRect>{{0, 0, 640, 480}, {0, 0, 800, 600}, {0, 0, 2560, 1080}}) {
        Layout(viewport.width, viewport.height);
        ASSERT_TRUE(Run(R"lua(
            local _, _, width, height = UI.Root:GetRect()
            for _, name in ipairs({'LobbyRoot', 'LobbyScroll', 'BackButton', 'StartButton'}) do
                local left, top, controlWidth, controlHeight = frames[name]:GetRect()
                assert(left >= 0 and top >= 0 and left + controlWidth <= width and top + controlHeight <= height, name)
            end
        )lua"));
        EXPECT_TRUE(HasText("Match"));
        Click("BackButton");
        ASSERT_TRUE(Run("assert(destination == 'MainMenu')"));
    }
}

TEST_F(LobbyUiTest, PreservesThreeRailsAndNumberedLoadout) {
    Layout(1280, 720);
    ASSERT_TRUE(Run(R"lua(
        local left, top, width = frames.DeploymentRail:GetRect()
        local center, centerTop, centerWidth = frames.BriefingRail:GetRect()
        local right, rightTop = frames.LoadoutRail:GetRect()
        assert(math.abs(left + width - center) < 1 and math.abs(center + centerWidth - right) < 1)
        assert(top == centerTop and top == rightTop)
        assert(frames.LoadoutSlot1 and frames.LoadoutSlot5)
        assert(frames.TowerInventory and frames.PartyChat and frames.LevelSelector)
    )lua"));
    EXPECT_TRUE(HasText("Empty slot"));
    EXPECT_TRUE(HasText("$150"));
    EXPECT_TRUE(std::any_of(renderer->commands.begin(), renderer->commands.end(), [](const auto& command) {
        return command.type == LambUI::RenderCommandType::DrawQuad && command.color == 0x0C1211FFu && command.height == 38;
    }));
    EXPECT_EQ(imageLoads, 4);
}

TEST_F(LobbyUiTest, MissionBoardPreviewsThenConfirmsOrCancelsWithoutClickThrough) {
    Click("ChooseLevelButton");
    ASSERT_TRUE(Run("assert(frames.LevelSelector:IsVisible())"));
    EXPECT_TRUE(HasText("MISSION BOARD"));
    EXPECT_TRUE(HasText("Second level"));
    EXPECT_GE(std::count_if(renderer->commands.begin(), renderer->commands.end(), [](const auto& command) {
        return command.textureHandle == reinterpret_cast<void*>(uintptr_t{123});
    }), 4);
    Click("LevelCard2");
    ASSERT_TRUE(Run("assert(selectedIndex == nil and snapshot.selectedLevel == 1)"));
    Click("HostButton");
    ASSERT_TRUE(Run("assert(hostedName == nil)"));
    Click("CancelLevelButton");
    ASSERT_TRUE(Run("assert(not frames.LevelSelector:IsVisible() and snapshot.selectedLevel == 1)"));
    Click("ChooseLevelButton");
    Click("LevelCard2");
    Click("ConfirmLevelButton");
    ASSERT_TRUE(Run("assert(not frames.LevelSelector:IsVisible() and selectedIndex == 2)"));
    EXPECT_TRUE(HasText("Second level"));
    EXPECT_EQ(imageLoads, 4);
}

TEST_F(LobbyUiTest, SmallMissionBoardPaginatesAndFitsTheViewport) {
    Click("ChooseLevelButton");
    Layout(640, 480);
    ASSERT_TRUE(Run(R"lua(
        assert(frames.LevelCard1:IsVisible() and not frames.LevelCard2:IsVisible())
        for _, name in ipairs({'LevelSelectorPanel', 'LevelCard1', 'ConfirmLevelButton', 'CancelLevelButton'}) do
            local left, top, width, height = frames[name]:GetRect()
            assert(left >= 0 and top >= 0 and left + width <= 640 and top + height <= 480, name)
        end
    )lua"));
    Click("NextLevelButton");
    ASSERT_TRUE(Run("assert(not frames.LevelCard1:IsVisible() and frames.LevelCard2:IsVisible())"));
    Click("LevelCard2");
    Click("ConfirmLevelButton");
    ASSERT_TRUE(Run("assert(selectedIndex == 2)"));
}

class OptionsUiTest : public SplashUiTest {
    protected:
        void SetUp() override {
                SplashUiTest::SetUp();
    bindings->SetFontResolver([](const std::string& name, int size) -> void* {
        EXPECT_EQ(name, "Inter-Bold");
        EXPECT_EQ(size, 32);
        return reinterpret_cast<void*>(uintptr_t{456});
    });
    bindings->SetImageLoader([](const std::string& path) {
        EXPECT_EQ(path, "assets/images/splash_screen.png");
        return LambUI::UIImage{reinterpret_cast<void*>(uintptr_t{123}), 1376, 768};
    });
    ASSERT_TRUE(Run(R"lua(
        Audio = { Preload = function(...) end, Play = function(...) end }
        Scene = { GoTo = function(scene) destination = scene end }
        Settings = {
            Get = function() return {
                fullscreen = false, exclusiveFullscreen = false, vSyncEnabled = true,
                displayWidth = 1280, displayHeight = 720, refreshRate = 60,
                graphicsQuality = 2, masterVolume = 0.8, musicVolume = 0.7,
                sfxVolume = 0.8, audioDevice = '', muteWhenUnfocused = true
            } end,
            DisplayModes = function() return {{width = 1280, height = 720, refreshRate = 60}} end,
            AudioDevices = function() return {} end,
            Apply = function(settings)
                saved = {}
                for key, value in pairs(settings) do saved[key] = value end
                for key, value in pairs(settings) do active[key] = value end
                return true
            end,
            SetVolume = function(field, value)
                active[field] = value
                saved = {}
                for key, current in pairs(active) do saved[key] = current end
                volumeUpdates = volumeUpdates + 1
                return true
            end
        }
        active = Settings.Get()
        volumeUpdates = 0
        Settings.Defaults = Settings.Get
    )lua"));
    const auto script = std::filesystem::path(NODESPIRE_MAINMENU_SCRIPT).parent_path() / "Options.lua";
    ASSERT_EQ(luaL_dofile(lua.get(), script.string().c_str()), LUA_OK) << lua_tostring(lua.get(), -1);
        Layout(1920, 1080);
    }

    void Layout(float width, float height) {
        manager.SetDisplaySize(width, height);
        manager.Update(0);
        ASSERT_TRUE(Run("OnUpdate()"));
        manager.Update(0);
        manager.Render();
    }

    void Click(float x, float y) {
        manager.InjectMouseMove(x, y);
        manager.InjectMouseButton(LambUI::MouseButton::Left, true);
        manager.InjectMouseButton(LambUI::MouseButton::Left, false);
        manager.Update(0);
        manager.Render();
    }

    void Drag(float startX, float startY, float endX, float endY) {
        manager.InjectMouseMove(startX, startY);
        manager.InjectMouseButton(LambUI::MouseButton::Left, true);
        manager.InjectMouseMove(endX, endY);
        manager.InjectMouseButton(LambUI::MouseButton::Left, false);
        manager.Update(0);
        manager.Render();
    }

    bool HasText(const std::string& text) {
        return std::any_of(renderer->commands.begin(), renderer->commands.end(), [&](const auto& command) {
            return command.type == LambUI::RenderCommandType::DrawString && command.text == text;
        });
    }
};

TEST_F(OptionsUiTest, LoadsSettingsAndDoesNotSaveOnBack) {
    for (const auto& text : {"Options", "Display", "Audio", "Apply", "Defaults", "Back"}) {
        EXPECT_TRUE(std::any_of(renderer->commands.begin(), renderer->commands.end(), [&](const auto& command) {
            return command.type == LambUI::RenderCommandType::DrawString && command.text == text;
        })) << text;
    }
    ASSERT_TRUE(Run("assert(saved == nil)"));
    Click(680, 816);
    ASSERT_TRUE(Run("assert(destination == 'MainMenu' and saved == nil)"));
}

TEST_F(OptionsUiTest, VolumesApplyImmediatelyWhileOtherSettingsRemainDraft) {
    Layout(800, 600);
    Click(72, 400);
    EXPECT_TRUE(HasText("Unsaved changes"));
    Click(680, 532);
    ASSERT_TRUE(Run("assert(saved and saved.vSyncEnabled == false)"));
    Click(244, 132);
    EXPECT_TRUE(HasText("80%"));
    Drag(400, 218, 358, 218);
    EXPECT_TRUE(HasText("50%"));
    ASSERT_TRUE(Run("assert(math.abs(active.masterVolume - 0.5) < 0.01 and math.abs(saved.masterVolume - 0.5) < 0.01)"));
    Click(680, 532);
    ASSERT_TRUE(Run("assert(math.abs(saved.masterVolume - 0.5) < 0.01)"));
    Click(540, 532);
    EXPECT_TRUE(HasText("80%"));
    ASSERT_TRUE(Run("assert(math.abs(saved.masterVolume - 0.8) < 0.001 and saved.vSyncEnabled == false)"));
    Click(680, 532);
    ASSERT_TRUE(Run("assert(math.abs(saved.masterVolume - 0.8) < 0.001 and saved.vSyncEnabled == true)"));
}

TEST_F(OptionsUiTest, VolumeChangesDoNotApplyPendingDisplaySettingsOrNeedApplyBeforeBack) {
    Layout(800, 600);
    ASSERT_TRUE(Run("assert(volumeUpdates == 0 and saved == nil)"));
    Click(72, 400);
    Click(244, 132);
    Drag(400, 218, 358, 218);
    Drag(400, 306, 358, 306);
    Drag(400, 394, 358, 394);
    ASSERT_TRUE(Run(R"lua(
        assert(volumeUpdates == 3)
        assert(math.abs(active.masterVolume - 0.5) < 0.01)
        assert(math.abs(active.musicVolume - 0.5) < 0.01)
        assert(math.abs(active.sfxVolume - 0.5) < 0.01)
        assert(saved.vSyncEnabled == true and saved.audioDevice == '')
    )lua"));
    EXPECT_TRUE(HasText("Unsaved changes"));
    Click(100, 532);
    ASSERT_TRUE(Run("assert(destination == 'MainMenu' and math.abs(saved.masterVolume - 0.5) < 0.01)"));
}

TEST_F(OptionsUiTest, VolumeSaveFailuresAreReported) {
    Layout(800, 600);
    ASSERT_TRUE(Run("Settings.SetVolume = function() return false, 'Volume save failed' end"));
    Click(244, 132);
    Drag(400, 218, 358, 218);
    EXPECT_TRUE(HasText("Volume save failed"));
    ASSERT_TRUE(Run("assert(saved == nil)"));
}

TEST_F(OptionsUiTest, WindowModeSelectionAndSaveErrors) {
    Layout(640, 480);
    EXPECT_TRUE(HasText("Windowed"));
    EXPECT_FALSE(HasText("Windowed  v"));
    Click(184, 224);
    EXPECT_TRUE(HasText("Borderless fullscreen"));
    Click(184, 280);
    Click(540, 412);
    ASSERT_TRUE(Run("assert(saved.fullscreen and not saved.exclusiveFullscreen)"));
    ASSERT_TRUE(Run("Settings.Apply = function() return false, 'Save failed' end"));
    Click(540, 412);
    EXPECT_TRUE(HasText("Save failed"));
    EXPECT_FALSE(HasText("Settings applied"));
}

TEST_F(OptionsUiTest, SplashArtworkSurroundsResponsiveTranslucentPanel) {
    for (const auto& viewport : std::vector<LambUI::UIRect>{{0, 0, 1920, 1080}, {0, 0, 640, 480}, {0, 0, 640, 720}}) {
        Layout(viewport.width, viewport.height);
        const auto image = std::find_if(renderer->commands.begin(), renderer->commands.end(), [](const auto& command) {
            return command.textureHandle == reinterpret_cast<void*>(uintptr_t{123});
        });
        const auto panel = std::find_if(renderer->commands.begin(), renderer->commands.end(), [](const auto& command) {
            return command.type == LambUI::RenderCommandType::DrawQuad && command.color == 0x141719D9u;
        });
        ASSERT_NE(image, renderer->commands.end());
        ASSERT_NE(panel, renderer->commands.end());
        EXPECT_LT(image, panel);
        EXPECT_FLOAT_EQ(panel->width, std::min(760.0f, viewport.width - 48));
        EXPECT_FLOAT_EQ(panel->height, std::min(640.0f, viewport.height - 48));
        EXPECT_FLOAT_EQ(panel->x, (viewport.width - panel->width) / 2);
        EXPECT_FLOAT_EQ(panel->y, (viewport.height - panel->height) / 2);
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