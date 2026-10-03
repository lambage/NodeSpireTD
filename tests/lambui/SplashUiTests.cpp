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
        for (const auto& command : commands) {
            if (command.type == LambUI::RenderCommandType::CustomCallback && command.customRenderFunc) {
                command.customRenderFunc({command.x, command.y, command.width, command.height, command.customRenderUserData});
            }
        }
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

class PlayUiTest : public SplashUiTest {
  protected:
    void SetUp() override {
        SplashUiTest::SetUp();
        bindings->SetFontResolver([](const std::string&, int) -> void* {
            return reinterpret_cast<void*>(uintptr_t{456});
        });
        ASSERT_TRUE(Run(R"lua(
            Audio = {Preload = function() end, Play = function() end}
            frames = {}
            local create = UI.CreateFrame
            UI.CreateFrame = function(kind, name, parent)
                local widget = create(kind, name, parent)
                frames[name] = widget
                return widget
            end
            snapshot = {
                phase = 'loading', level = 'Grassy', headline = 'Loading Grassy', description = 'Loading assets',
                health = 100, money = 250, wave = 1, waveCount = 5, enemies = 0, startReason = '',
                canStart = false, paused = false, client = false, online = false, loadoutVisible = false, countdownVisible = false,
                selectedSlot = 0, placement = '', slots = {}, chat = {}, masterVolume = 1, musicVolume = 0.5, sfxVolume = 0.8
            }
            for index = 1, 5 do snapshot.slots[index] = {name = 'Arrow tower', cost = 100, available = true} end
            Play = {
                State = function() return snapshot end,
                Preview = function(slot, left, top, width, height)
                    previews = previews or {}
                    previews[slot] = {left, top, width, height}
                    previewCalls = (previewCalls or 0) + 1
                    return true
                end,
                Start = function() started = true; snapshot.phase = 'running'; return true end,
                SelectSlot = function(slot) selected = slot; snapshot.selectedSlot = slot; return true end,
                Pause = function(value) snapshot.paused = value; return true end,
                Lobby = function() destination = 'Lobby'; return true end,
                CancelPlacement = function() snapshot.selectedSlot = 0; return true end,
                ClearSelection = function() snapshot.selection = nil; return true end,
                Upgrade = function(id) upgraded = id; return true end,
                Sell = function() sold = true; return true end,
                Restart = function() restarted = true; return true end,
                Retry = function() retried = true; return true end,
                SendChat = function(value) sent = value; return true end,
                SetVolume = function(name, value) snapshot[name] = value; return true end
            }
        )lua"));
        const auto path = std::filesystem::path(NODESPIRE_MAINMENU_SCRIPT).parent_path() / "PlayLevel.lua";
        ASSERT_EQ(luaL_dofile(lua.get(), path.string().c_str()), LUA_OK) << lua_tostring(lua.get(), -1);
        manager.Update(0);
        ASSERT_TRUE(Run("OnUpdate(0.1)"));
        manager.Update(0);
    }

    void Click(const char* name) {
        ASSERT_TRUE(Run((std::string("clickX, clickY, clickW, clickH = frames['") + name + "']:GetRect()").c_str()));
        lua_getglobal(lua.get(), "clickX");
        lua_getglobal(lua.get(), "clickY");
        lua_getglobal(lua.get(), "clickW");
        lua_getglobal(lua.get(), "clickH");
        const float mouseX = static_cast<float>(lua_tonumber(lua.get(), -4) + lua_tonumber(lua.get(), -2) / 2);
        const float mouseY = static_cast<float>(lua_tonumber(lua.get(), -3) + lua_tonumber(lua.get(), -1) / 2);
        lua_pop(lua.get(), 4);
        manager.InjectMouseMove(mouseX, mouseY);
        manager.InjectMouseButton(LambUI::MouseButton::Left, true);
        manager.InjectMouseButton(LambUI::MouseButton::Left, false);
        manager.Update(0);
    }
};

TEST_F(PlayUiTest, TowerCanvasesRenderAtResolvedBoundsAndRespectVisibility) {
    manager.Render();
    ASSERT_TRUE(Run("assert(previewCalls == nil)"));
    for (const auto& viewport : std::vector<LambUI::UIRect>{{0, 0, 1920, 1080}, {0, 0, 640, 480}}) {
        manager.SetDisplaySize(viewport.width, viewport.height);
        manager.Update(0);
        ASSERT_TRUE(Run("previewCalls = 0; snapshot.phase = 'running'; snapshot.loadoutVisible = true; OnUpdate(0.1); assert(previewCalls == 0)"));
        manager.Update(0);
        manager.Render();
        ASSERT_TRUE(Run(R"lua(
            assert(previewCalls == 5)
            for index = 1, 5 do
                local left, top, width, height = frames['TowerPreview' .. index]:GetRect()
                local drawn = previews[index]
                assert(width > 0 and height > 0)
                assert(drawn[1] == left and drawn[2] == top and drawn[3] == width and drawn[4] == height)
            end
        )lua"));
        Click("TowerPreview2");
        ASSERT_TRUE(Run("assert(selected == 2); previewCalls = 0; snapshot.paused = true; OnUpdate(0.1)"));
        manager.Update(0);
        manager.Render();
        ASSERT_TRUE(Run("assert(previewCalls == 0); snapshot.paused = false; snapshot.loadoutVisible = false; OnUpdate(0.1)"));
        manager.Update(0);
        manager.Render();
        ASSERT_TRUE(Run("assert(previewCalls == 0)"));
    }
}

TEST_F(PlayUiTest, LoadingReadyRunningAndPauseRouteActions) {
    ASSERT_TRUE(Run("assert(frames.MatchStatus:IsVisible() and not frames.TowerLoadout:IsVisible()); assert(PlayPointerOverHud(500, 400))"));
    ASSERT_TRUE(Run("snapshot.phase = 'ready'; snapshot.loadoutVisible = true; OnUpdate(0.1)"));
    manager.Update(0);
    Click("StartMatch");
    ASSERT_TRUE(Run("assert(not started); snapshot.canStart = true; OnUpdate(0.1)"));
    manager.Update(0);
    Click("StartMatch");
    ASSERT_TRUE(Run("assert(started); OnUpdate(0.1); assert(not frames.MatchStatus:IsVisible()); assert(not PlayPointerOverHud(960, 400))"));
    manager.Update(0);
    Click("TowerSlot2");
    ASSERT_TRUE(Run("assert(selected == 2); assert(PlayPointerOverHud(clickX + 10, clickY + 10))"));
    Click("MatchMenu");
    ASSERT_TRUE(Run("assert(snapshot.paused and frames.PauseOverlay:IsVisible()); assert(PlayPointerOverHud(960,400))"));
    Click("ResumeMatch");
    ASSERT_TRUE(Run("assert(not snapshot.paused)"));
}

TEST_F(PlayUiTest, LayoutAndTerminalControlsRemainInsideSmallWindows) {
    for (const auto& viewport : std::vector<LambUI::UIRect>{{0, 0, 640, 480}, {0, 0, 1280, 720}, {0, 0, 2560, 1080}}) {
        manager.SetDisplaySize(viewport.width, viewport.height);
        manager.Update(0);
        ASSERT_TRUE(Run("snapshot.phase = 'running'; snapshot.loadoutVisible = true; OnUpdate(0.1)"));
        manager.Update(0);
        ASSERT_TRUE(Run(R"lua(
            local _, _, width, height = UI.Root:GetRect()
            for _, name in ipairs({'TowerLoadout', 'BattleStats', 'GoldPlaque', 'MatchMenu'}) do
                local left, top, span, tall = frames[name]:GetRect()
                assert(left >= 0 and top >= 0 and left + span <= width + 0.1 and top + tall <= height + 0.1, name)
            end
            snapshot.phase = 'victory'; snapshot.headline = 'Victory'; snapshot.loadoutVisible = false; OnUpdate(0.1)
            assert(frames.ReplayMatch:IsVisible() and not frames.TowerLoadout:IsVisible())
        )lua"));
        manager.Update(0);
        Click("ReplayMatch");
        ASSERT_TRUE(Run("assert(restarted); restarted = false; snapshot.client = true; OnUpdate(0.1); assert(not frames.ReplayMatch:IsVisible()); snapshot.client = false"));
    }
}

TEST_F(PlayUiTest, ProfilesRespectOwnershipAndRouteUpgradeActions) {
    ASSERT_TRUE(Run(R"lua(
        snapshot.phase, snapshot.loadoutVisible = 'running', true
        snapshot.selection = {
            kind = 'tower', archetype = 'archer_hut', id = 1, name = 'Arrow tower', bio = 'Long range defense', owned = true,
            damage = 10, range = 12, rate = 1, spent = 100, sell = 80, damageType = 'PHY', armorPiercing = 0,
            effects = 'Burn 2/s', upgrades = {{id = 'quickdraw_rig', name = 'Quickdraw Rig', level = 0, maxLevel = 2,
                cost = 50, enabled = true, description = 'Extra damage', reason = ''}}
        }
        OnUpdate(0.1)
        assert(frames.SelectionProfile:IsVisible())
        assert(frames.TowerLoadout:IsVisible() and frames.TowerPreview1:IsVisible())
    )lua"));
    manager.Update(0);
    Click("archer_hut_Upgrade1");
    ASSERT_TRUE(Run("assert(upgraded == 'quickdraw_rig'); upgraded = nil; snapshot.selection.owned = false; snapshot.selection.upgrades[1].enabled = false; OnUpdate(0.1)"));
    manager.Update(0);
    Click("archer_hut_Upgrade1");
    Click("SellTower");
    ASSERT_TRUE(Run("assert(not upgraded and not sold); snapshot.selection.owned = true; OnUpdate(0.1)"));
    manager.Update(0);
    Click("SellTower");
    ASSERT_TRUE(Run("assert(sold)"));
    ASSERT_TRUE(Run(R"lua(
        snapshot.selection = {kind = 'enemy', id = 2, name = 'Scout', bio = 'Fast enemy', health = 25, maxHealth = 35,
            shield = 0, maxShield = 0, armor = 1, speed = 3, reward = 8, baseDamage = 5, resistances = 'FIR 20%'}
        OnUpdate(0.1)
        assert(not frames.archer_hut_TowerContent:IsVisible() and not frames.SellTower:IsVisible())
    )lua"));
    manager.Update(0);
    manager.Render();
    EXPECT_TRUE(std::any_of(renderer->commands.begin(), renderer->commands.end(), [](const auto& command) {
        return command.text.find("HEALTH 25 / 35") != std::string::npos;
    }));
    Click("CloseProfile");
    ASSERT_TRUE(Run("assert(not snapshot.selection and frames.TowerLoadout:IsVisible() and frames.TowerPreview1:IsVisible())"));
}

TEST_F(PlayUiTest, RealTowerTreesFitAndCanScrollAtSmallSizes) {
    const auto assetRoot = std::filesystem::path(NODESPIRE_MAINMENU_SCRIPT).parent_path().parent_path();
    lua_pushstring(lua.get(), assetRoot.generic_string().c_str());
    lua_setglobal(lua.get(), "assetRoot");
    for (const char* archetype : {"archer_hut", "mage_tower", "archer_hut"}) {
        lua_pushstring(lua.get(), archetype);
        lua_setglobal(lua.get(), "archetype");
        ASSERT_TRUE(Run(R"lua(
            local definition = dofile(assetRoot .. '/models/towers/' .. archetype .. '/' .. archetype .. '.tower.lua')
            snapshot.phase = 'running'
            snapshot.selection = {kind = 'tower', id = archetype, archetype = archetype, name = definition.displayName,
                owned = true, damage = 20, range = 5, rate = 1, spent = 150, sell = 120, damageType = 'PHY', armorPiercing = 1,
                upgrades = {}}
            for index, node in ipairs(definition.upgradeTree.nodes) do
                snapshot.selection.upgrades[index] = {id = node.id, name = node.displayName, description = node.description,
                    level = 0, maxLevel = #node.upgradeLevels, cost = node.upgradeLevels[1].cost,
                    requires = node.requires, enabled = index == 1}
            end
        )lua"));
        manager.SetDisplaySize(1280, 720);
        manager.Update(0);
        ASSERT_TRUE(Run("OnUpdate(0.1)"));
        manager.Update(0);
        ASSERT_TRUE(Run(R"lua(
            local left, top, width, height = frames[archetype .. '_TalentTree']:GetRect()
            for index in ipairs(snapshot.selection.upgrades) do
                local nodeX, nodeY, nodeWidth, nodeHeight = frames[archetype .. '_Upgrade' .. index]:GetRect()
                assert(nodeX >= left and nodeX + nodeWidth <= left + width)
                assert(nodeY >= top and nodeY + nodeHeight <= top + height)
            end
            local other = archetype == 'archer_hut' and 'mage_tower' or 'archer_hut'
            assert(frames[archetype .. '_TowerContent']:IsVisible())
            assert(not frames[other .. '_TowerContent'] or not frames[other .. '_TowerContent']:IsVisible())
            local _, profileTop, _, profileHeight = frames.SelectionProfile:GetRect()
            local _, loadoutTop = frames.TowerLoadout:GetRect()
            assert(profileTop + profileHeight <= loadoutTop)
        )lua"));
        Click((std::string(archetype) + "_Upgrade1").c_str());
        ASSERT_TRUE(Run("assert(upgraded == snapshot.selection.upgrades[1].id); upgraded = nil"));
        manager.SetDisplaySize(640, 480);
        manager.Update(0);
        ASSERT_TRUE(Run("OnUpdate(0.1)"));
        manager.Update(0);
        ASSERT_TRUE(Run("local _, top = frames[archetype .. '_Upgrade1']:GetRect(); beforeScroll = top; frames.ProfileScroll:SetScrollOffset(0, 150)"));
        manager.Update(0);
        ASSERT_TRUE(Run("local _, top = frames[archetype .. '_Upgrade1']:GetRect(); assert(top < beforeScroll - 100)"));
    }
}

TEST_F(PlayUiTest, ProfileCanMoveWithoutHidingLoadoutOrResettingOnRefresh) {
    ASSERT_TRUE(Run(R"lua(
        snapshot.phase, snapshot.loadoutVisible = 'running', true
        snapshot.selection = {kind = 'tower', archetype = 'archer_hut', id = 1, name = 'Archer Hut', owned = true,
            damage = 20, range = 5, rate = 1, spent = 150, sell = 120, damageType = 'PHY', armorPiercing = 1,
            upgrades = {{id = 'quickdraw_rig', name = 'Quickdraw Rig', level = 0, maxLevel = 2, cost = 50, enabled = true}}}
        OnUpdate(0.1)
    )lua"));
    manager.Update(0);
    ASSERT_TRUE(Run("beforeX, beforeY = frames.SelectionProfile:GetRect()"));
    lua_getglobal(lua.get(), "beforeX");
    lua_getglobal(lua.get(), "beforeY");
    const float left = static_cast<float>(lua_tonumber(lua.get(), -2));
    const float top = static_cast<float>(lua_tonumber(lua.get(), -1));
    lua_pop(lua.get(), 2);
    manager.InjectMouseMove(left + 80, top + 16);
    manager.InjectMouseButton(LambUI::MouseButton::Left, true);
    manager.InjectMouseMove(left - 120, top + 48);
    manager.InjectMouseButton(LambUI::MouseButton::Left, false);
    manager.Update(0);
    ASSERT_TRUE(Run("OnUpdate(0.1)"));
    manager.Update(0);
    ASSERT_TRUE(Run(R"lua(
        local left, top = frames.SelectionProfile:GetRect()
        assert(math.abs(left - (beforeX - 200)) < 1 and math.abs(top - (beforeY + 32)) < 1)
        assert(PlayPointerOverHud(left + 10, top + 10))
        assert(frames.TowerLoadout:IsVisible() and frames.TowerPreview1:IsVisible())
    )lua"));
    Click("TowerSlot2");
    ASSERT_TRUE(Run("assert(selected == 2)"));
    manager.SetDisplaySize(640, 480);
    manager.Update(0);
    ASSERT_TRUE(Run("OnUpdate(0.1)"));
    manager.Update(0);
    ASSERT_TRUE(Run(R"lua(
        local left, top, width, height = frames.SelectionProfile:GetRect()
        assert(left >= 0 and top >= 0 and left + width <= 640 and top + height <= 480)
    )lua"));
}

TEST_F(PlayUiTest, TalentTreeRendersBranchesArtworkAndLockedStates) {
    int imageLoads = 0;
    bindings->SetImageLoader([&](const std::string& path) {
        ++imageLoads;
        EXPECT_EQ(path.find("assets/images/talents/archer/archer_"), 0u);
        return LambUI::UIImage{reinterpret_cast<void*>(uintptr_t{123}), 406, 406};
    });
    ASSERT_TRUE(Run(R"lua(
        snapshot.phase = 'running'
        snapshot.selection = {
            kind = 'tower', archetype = 'archer_hut', id = 1, name = 'Archer Hut', owned = true,
            damage = 20, range = 5, rate = 1, spent = 240, sell = 192, damageType = 'PHY', armorPiercing = 1,
            upgrades = {
                {id = 'quickdraw_rig', name = 'Quickdraw Rig', level = 1, maxLevel = 2, cost = 150, enabled = true},
                {id = 'hardened_draw', name = 'Hardened Draw', level = 1, maxLevel = 1, cost = 0, enabled = false,
                    requires = {'quickdraw_rig'}, reason = 'Maximum level reached.'},
                {id = 'stone_specialization', name = 'Stone Specialization', level = 0, maxLevel = 1, cost = 180,
                    enabled = true, requires = {'hardened_draw'}, minUpgradesRequired = 2},
                {id = 'metal_specialization', name = 'Metal Specialization', level = 0, maxLevel = 1, cost = 180,
                    enabled = false, requires = {'hardened_draw'}, reason = 'Locked by your chosen specialization.'}
            }
        }
        OnUpdate(0.1)
    )lua"));
    manager.Update(0);
    manager.Render();
    EXPECT_EQ(imageLoads, 4);
    EXPECT_TRUE(std::any_of(renderer->commands.begin(), renderer->commands.end(), [](const auto& command) {
        return command.textureHandle == reinterpret_cast<void*>(uintptr_t{123}) && command.color == 0x50585FFFu;
    }));
    EXPECT_TRUE(std::any_of(renderer->commands.begin(), renderer->commands.end(), [](const auto& command) {
        return command.type == LambUI::RenderCommandType::DrawQuad && command.color == 0xE1BD67FFu && command.width == 2;
    }));
    for (const auto& viewport : std::vector<LambUI::UIRect>{{0, 0, 1920, 1080}, {0, 0, 1280, 720}, {0, 0, 640, 480}}) {
        manager.SetDisplaySize(viewport.width, viewport.height);
        manager.Update(0);
        ASSERT_TRUE(Run("OnUpdate(0.1)"));
        manager.Update(0);
        ASSERT_TRUE(Run(R"lua(
            local _, firstY, nodeWidth, nodeHeight = frames.archer_hut_Upgrade1:GetRect()
            local _, secondY = frames.archer_hut_Upgrade2:GetRect()
            local thirdX, thirdY = frames.archer_hut_Upgrade3:GetRect()
            local fourthX, fourthY = frames.archer_hut_Upgrade4:GetRect()
            assert(nodeWidth == 52 and nodeHeight == 52)
            assert(secondY > firstY + nodeHeight and thirdY > secondY + nodeHeight)
            assert(thirdY == fourthY and fourthX >= thirdX + nodeWidth + 20)
            local left, top, width, height = frames.SelectionProfile:GetRect()
            local sellX, sellY, sellWidth, sellHeight = frames.SellTower:GetRect()
            assert(sellX >= left and sellX + sellWidth <= left + width)
            assert(sellY >= top and sellY + sellHeight <= top + height)
            assert(frames.archer_hut_TalentLink9:IsVisible())
        )lua"));
    }
    EXPECT_EQ(imageLoads, 4);
    manager.SetDisplaySize(1920, 1080);
    manager.Update(0);
    ASSERT_TRUE(Run("OnUpdate(0.1)"));
    manager.Update(0);
    Click("archer_hut_Upgrade4");
    ASSERT_TRUE(Run("assert(not upgraded)"));
    Click("archer_hut_Upgrade3");
    ASSERT_TRUE(Run("assert(upgraded == 'stone_specialization'); snapshot.selection.upgrades = {snapshot.selection.upgrades[1]}; OnUpdate(0.1)"));
    manager.Update(0);
    ASSERT_TRUE(Run("assert(not frames.archer_hut_Upgrade2:IsVisible() and not frames.archer_hut_TalentLink1:IsVisible())"));
}

TEST_F(PlayUiTest, UnregisteredTowerDoesNotInferATalentLayout) {
    ASSERT_TRUE(Run(R"lua(
        snapshot.phase, snapshot.loadoutVisible = 'running', true
        snapshot.selection = {kind = 'tower', archetype = 'future_tower', id = 1, name = 'Future Tower', owned = true,
            damage = 20, range = 5, rate = 1, spent = 150, sell = 120, damageType = 'PHY', armorPiercing = 1,
            upgrades = {{id = 'future_power', name = 'Power', level = 0, maxLevel = 1, cost = 50, enabled = true}}}
        OnUpdate(0.1)
        assert(not frames.future_tower_Upgrade1 and frames.SellTower:IsVisible())
        assert(frames.TowerLoadout:IsVisible())
    )lua"));
    manager.Update(0);
    manager.Render();
    EXPECT_TRUE(std::any_of(renderer->commands.begin(), renderer->commands.end(), [](const auto& command) {
        return command.text == "Tower UI not configured.";
    }));
    Click("SellTower");
    ASSERT_TRUE(Run("assert(sold)"));
    Click("CloseProfile");
    ASSERT_TRUE(Run("assert(not snapshot.selection and not frames.future_tower_TowerContent:IsVisible())"));
}

TEST_F(PlayUiTest, ChatLoadFailureAndReturnActionsAreUsable) {
    ASSERT_TRUE(Run("snapshot.phase = 'failed'; snapshot.headline = 'Deployment failed'; OnUpdate(0.1)"));
    manager.Update(0);
    Click("RetryLoad");
    ASSERT_TRUE(Run("assert(retried); snapshot.phase = 'running'; snapshot.online = true; snapshot.chat = {'Player: Ready'}; OnUpdate(0.1)"));
    manager.Update(0);
    ASSERT_TRUE(Run("frames.ChatInput:SetText('Ready'); assert(frames.MatchChat:IsVisible())"));
    Click("SendChat");
    ASSERT_TRUE(Run("assert(sent == 'Ready' and frames.ChatInput:GetText() == '')"));
    Click("MatchMenu");
    Click("PauseLobby");
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