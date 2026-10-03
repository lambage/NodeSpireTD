#include "lambui/scenes/MainMenuScene.hpp"

#include "AudioEngine.hpp"
#include "lambui/AppControl.hpp"
#include "lambui_backend/VulkanUiRenderer.hpp"
#include "lua.hpp"

#include <LambUI/UIButton.h>
#include <LambUI/UIEvent.h>
#include <LambUI/UIImageWidget.h>
#include <LambUI/UIManager.h>
#include <LambUI/UITextWidget.h>
#include <LambUI/UITypes.h>
#include <LambUI/UIWidget.h>

#include <stdexcept>
#include <string_view>
#include <utility>

namespace NodeSpireUi {
namespace {

constexpr const char* kHoverSfx = "assets/audio/hover.ogg";
constexpr const char* kClickSfx = "assets/audio/click.ogg";
constexpr const char* kCloseSfx = "assets/audio/close.ogg";

std::string argument(lua_State* lua, int index, const char* name) {
    if (lua_type(lua, index) != LUA_TSTRING) {
        throw std::runtime_error(std::string("ui.createButton expected string for ") + name);
    }
    return lua_tostring(lua, index);
}

float numberArgument(lua_State* lua, int index, const char* name) {
    if (!lua_isnumber(lua, index)) {
        throw std::runtime_error(std::string("ui.createButton expected number for ") + name);
    }
    return static_cast<float>(lua_tonumber(lua, index));
}

} // namespace

MainMenuScene::MainMenuScene(lambui_backend::VulkanUiRenderer& renderer)
    : LuaUiScene("assets/scenes/MainMenu.lua", renderer) {}

MainMenuScene& MainMenuScene::fromLua(lua_State* lua) {
    return *static_cast<MainMenuScene*>(lua_touserdata(lua, lua_upvalueindex(1)));
}

int MainMenuScene::luaReset(lua_State* lua) {
    fromLua(lua).buttonSpecs_.clear();
    return 0;
}

int MainMenuScene::luaCreateButton(lua_State* lua) {
    auto& self = fromLua(lua);
    try {
        MenuButtonSpec spec;
        spec.text = argument(lua, 1, "text");
        spec.action = argument(lua, 2, "action");
        spec.yOffset = numberArgument(lua, 3, "yOffset");
        spec.imageName = argument(lua, 4, "imageName");
        self.buttonSpecs_.push_back(std::move(spec));
    } catch (const std::exception& error) {
        return luaL_error(lua, "ui.createButton failed: %s", error.what());
    }
    return 0;
}

void MainMenuScene::bindSceneApi(lua_State* lua, AudioEngine&) {
    lua_newtable(lua);
    lua_pushlightuserdata(lua, this);
    lua_pushcclosure(lua, luaReset, 1);
    lua_setfield(lua, -2, "reset");
    lua_pushlightuserdata(lua, this);
    lua_pushcclosure(lua, luaCreateButton, 1);
    lua_setfield(lua, -2, "createButton");
    lua_setglobal(lua, "ui");
}

void MainMenuScene::buildUi(AudioEngine& audio) {
    auto& manager = uiManager();
    auto& root = manager.GetRoot();

    auto* screen = root.CreateChild<LambUI::UIWidget>("MainMenuRoot");
    screen->SetAllPoints(&root);
    LambUI::UIStyle style;
    style.fillColor = 0x000000FFu;
    screen->SetStyle(style);

    auto* background = screen->CreateChild<LambUI::UIImageWidget>("MainMenuBackground");
    background->SetAllPoints(screen);
    background->SetMouseEnabled(false);
    background->SetFit(LambUI::ImageFit::Contain);
    background->SetImageLoader([renderer = &renderer()](const std::string& path) {
        const auto image = renderer->LoadImage(path);
        return LambUI::UIImage{image.handle, image.width, image.height};
    });

    if (!background->SetSource("assets/images/splash_screen.png")) {
        auto* title = screen->CreateChild<LambUI::UITextWidget>("MainMenuTitle");
        title->SetText("NodeSpire TD");
        title->SetPoint(LambUI::AnchorPoint::Top, screen, LambUI::AnchorPoint::Top, 0.0f, 40.0f);
    }

    audio.preload(kHoverSfx, AudioChannel::Sfx);
    audio.preload(kClickSfx, AudioChannel::Sfx);
    audio.preload(kCloseSfx, AudioChannel::Sfx);

    for (std::size_t index = 0; index < buttonSpecs_.size(); ++index) {
        const auto& spec = buttonSpecs_[index];
        auto* button = screen->CreateChild<LambUI::UIButton>("MainMenuButton" + std::to_string(index));
        button->SetSize(260.0f, 110.0f);
        button->SetPoint(LambUI::AnchorPoint::Center, screen, LambUI::AnchorPoint::Center, 0.0f, spec.yOffset);

        const std::string normalPath = "assets/images/" + spec.imageName + ".png";
        const std::string hoverPath = "assets/images/" + spec.imageName + "_hover.png";
        lambui_backend::VulkanUiRenderer::Image normalImage;
        lambui_backend::VulkanUiRenderer::Image hoverImage;
        try {
            normalImage = renderer().LoadImage(normalPath);
        } catch (const std::exception&) {
        }
        try {
            hoverImage = renderer().LoadImage(hoverPath);
        } catch (const std::exception&) {
        }
        const bool hasNormal = normalImage.handle != nullptr;
        const void* hoverHandle = hoverImage.handle ? hoverImage.handle : normalImage.handle;
        const bool hasHover = hoverHandle != nullptr;

        if (hasNormal) {
            button->SetTexture(normalImage.handle);
            button->SetNormalColor(0xFFFFFFFFu);
            button->SetHoverColor(0xFFFFFFFFu);
            button->SetPressedColor(0xFFFFFFFFu);
        } else {
            button->SetNormalColor(0x3A3F4BFFu);
            button->SetHoverColor(0x4C5566FFu);
            button->SetPressedColor(0x2A2E38FFu);
            auto* label = button->CreateChild<LambUI::UITextWidget>("MainMenuButtonLabel" + std::to_string(index));
            label->SetText(spec.text);
            label->SetAllPoints(button);
            label->SetMouseEnabled(false);
        }

        button->RegisterCallback(LambUI::UIEventType::OnMouseEnter, [button, &audio, hasHover, hoverHandle](const LambUI::UIEventData&) {
            if (hasHover) {
                button->SetTexture(const_cast<void*>(hoverHandle));
            }
            audio.play(kHoverSfx, AudioChannel::Sfx);
        });
        button->RegisterCallback(LambUI::UIEventType::OnMouseLeave, [button, hasNormal, normal = normalImage.handle](const LambUI::UIEventData&) {
            if (hasNormal) {
                button->SetTexture(normal);
            }
        });

        button->RegisterCallback(LambUI::UIEventType::OnClick, [this, &audio, action = spec.action](const LambUI::UIEventData&) {
            audio.play(kClickSfx, AudioChannel::Sfx);
            const std::string_view view(action);
            if (view == "Lobby") {
                requestTransition(SceneId::Lobby);
            } else if (view == "Options") {
                requestTransition(SceneId::Options);
            } else if (view == "Quit") {
                audio.play(kCloseSfx, AudioChannel::Sfx);
                RequestQuit();
            }
        });
    }
}

void MainMenuScene::onSceneEnter(AudioEngine& audio) {
    buildUi(audio);
}

} // namespace NodeSpireUi
