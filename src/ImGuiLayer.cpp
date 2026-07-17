#include "ImGuiLayer.hpp"

#include "VulkanContext.hpp"
#include "utility/VulkanTexture.hpp"
#include "lua.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <imgui_impl_vulkan.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

ImGuiKey ImGuiLayer::translateSfmlKeyToImGui(sf::Keyboard::Key key) {
    switch (key) {
    case sf::Keyboard::Key::A:
        return ImGuiKey_A;
    case sf::Keyboard::Key::B:
        return ImGuiKey_B;
    case sf::Keyboard::Key::C:
        return ImGuiKey_C;
    case sf::Keyboard::Key::D:
        return ImGuiKey_D;
    case sf::Keyboard::Key::E:
        return ImGuiKey_E;
    case sf::Keyboard::Key::F:
        return ImGuiKey_F;
    case sf::Keyboard::Key::G:
        return ImGuiKey_G;
    case sf::Keyboard::Key::H:
        return ImGuiKey_H;
    case sf::Keyboard::Key::I:
        return ImGuiKey_I;
    case sf::Keyboard::Key::J:
        return ImGuiKey_J;
    case sf::Keyboard::Key::K:
        return ImGuiKey_K;
    case sf::Keyboard::Key::L:
        return ImGuiKey_L;
    case sf::Keyboard::Key::M:
        return ImGuiKey_M;
    case sf::Keyboard::Key::N:
        return ImGuiKey_N;
    case sf::Keyboard::Key::O:
        return ImGuiKey_O;
    case sf::Keyboard::Key::P:
        return ImGuiKey_P;
    case sf::Keyboard::Key::Q:
        return ImGuiKey_Q;
    case sf::Keyboard::Key::R:
        return ImGuiKey_R;
    case sf::Keyboard::Key::S:
        return ImGuiKey_S;
    case sf::Keyboard::Key::T:
        return ImGuiKey_T;
    case sf::Keyboard::Key::U:
        return ImGuiKey_U;
    case sf::Keyboard::Key::V:
        return ImGuiKey_V;
    case sf::Keyboard::Key::W:
        return ImGuiKey_W;
    case sf::Keyboard::Key::X:
        return ImGuiKey_X;
    case sf::Keyboard::Key::Y:
        return ImGuiKey_Y;
    case sf::Keyboard::Key::Z:
        return ImGuiKey_Z;
    case sf::Keyboard::Key::Num0:
        return ImGuiKey_0;
    case sf::Keyboard::Key::Num1:
        return ImGuiKey_1;
    case sf::Keyboard::Key::Num2:
        return ImGuiKey_2;
    case sf::Keyboard::Key::Num3:
        return ImGuiKey_3;
    case sf::Keyboard::Key::Num4:
        return ImGuiKey_4;
    case sf::Keyboard::Key::Num5:
        return ImGuiKey_5;
    case sf::Keyboard::Key::Num6:
        return ImGuiKey_6;
    case sf::Keyboard::Key::Num7:
        return ImGuiKey_7;
    case sf::Keyboard::Key::Num8:
        return ImGuiKey_8;
    case sf::Keyboard::Key::Num9:
        return ImGuiKey_9;
    case sf::Keyboard::Key::Escape:
        return ImGuiKey_Escape;
    case sf::Keyboard::Key::LControl:
        return ImGuiKey_LeftCtrl;
    case sf::Keyboard::Key::LShift:
        return ImGuiKey_LeftShift;
    case sf::Keyboard::Key::LAlt:
        return ImGuiKey_LeftAlt;
    case sf::Keyboard::Key::RControl:
        return ImGuiKey_RightCtrl;
    case sf::Keyboard::Key::RShift:
        return ImGuiKey_RightShift;
    case sf::Keyboard::Key::RAlt:
        return ImGuiKey_RightAlt;
    case sf::Keyboard::Key::Space:
        return ImGuiKey_Space;
    case sf::Keyboard::Key::Enter:
        return ImGuiKey_Enter;
    case sf::Keyboard::Key::Backspace:
        return ImGuiKey_Backspace;
    case sf::Keyboard::Key::Tab:
        return ImGuiKey_Tab;
    case sf::Keyboard::Key::Left:
        return ImGuiKey_LeftArrow;
    case sf::Keyboard::Key::Right:
        return ImGuiKey_RightArrow;
    case sf::Keyboard::Key::Up:
        return ImGuiKey_UpArrow;
    case sf::Keyboard::Key::Down:
        return ImGuiKey_DownArrow;
    default:
        return ImGuiKey_None;
    }
}

ImGuiLayer::ImGuiLayer(lua_State* L) : L_(L) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    applyModernStyle();
    loadUiFonts();
    initLuaBindings();
}

ImGuiLayer::~ImGuiLayer() {
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiLayer::initializeVulkanBackend(const VulkanContext& context) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(context.extent().width), static_cast<float>(context.extent().height));

    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkFormat depthFormat = context.depthFormat();
    VkPipelineRenderingCreateInfo renderingCreateInfo{};
    renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &colorFormat;
    renderingCreateInfo.depthAttachmentFormat = depthFormat;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = context.instance();
    initInfo.PhysicalDevice = context.physicalDevice();
    initInfo.Device = context.device();
    initInfo.QueueFamily = context.graphicsQueueFamily();
    initInfo.Queue = context.graphicsQueue();
    initInfo.DescriptorPool = context.descriptorPool();

    const uint32_t imageCount = std::max(2u, context.swapchainImageCount());
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;

    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = renderingCreateInfo;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend.");
    }

    if (L_) {
        lua_pushlightuserdata(L_, const_cast<VulkanContext*>(&context));
        lua_setglobal(L_, "VulkanContext");
    }
}

void ImGuiLayer::processEvent(const sf::Event& event) {
    ImGuiIO& io = ImGui::GetIO();

    if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        io.DisplaySize = ImVec2(static_cast<float>(resized->size.x), static_cast<float>(resized->size.y));
    } else if (const auto* mouseMoved = event.getIf<sf::Event::MouseMoved>()) {
        io.MousePos = ImVec2(static_cast<float>(mouseMoved->position.x), static_cast<float>(mouseMoved->position.y));
    } else if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mousePressed->button == sf::Mouse::Button::Left) {
            io.MouseDown[0] = true;
        }
        if (mousePressed->button == sf::Mouse::Button::Right) {
            io.MouseDown[1] = true;
        }
    } else if (const auto* mouseReleased = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (mouseReleased->button == sf::Mouse::Button::Left) {
            io.MouseDown[0] = false;
        }
        if (mouseReleased->button == sf::Mouse::Button::Right) {
            io.MouseDown[1] = false;
        }
    } else if (const auto* mouseWheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
        io.MouseWheel += mouseWheel->delta;
    } else if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
        ImGuiKey imKey = translateSfmlKeyToImGui(keyPressed->code);
        if (imKey != ImGuiKey_None) {
            io.AddKeyEvent(imKey, true);
        }

        io.AddKeyEvent(ImGuiMod_Ctrl, keyPressed->control);
        io.AddKeyEvent(ImGuiMod_Shift, keyPressed->shift);
        io.AddKeyEvent(ImGuiMod_Alt, keyPressed->alt);
        io.AddKeyEvent(ImGuiMod_Super, keyPressed->system);
    } else if (const auto* keyReleased = event.getIf<sf::Event::KeyReleased>()) {
        ImGuiKey imKey = translateSfmlKeyToImGui(keyReleased->code);
        if (imKey != ImGuiKey_None) {
            io.AddKeyEvent(imKey, false);
        }

        io.AddKeyEvent(ImGuiMod_Ctrl, keyReleased->control);
        io.AddKeyEvent(ImGuiMod_Shift, keyReleased->shift);
        io.AddKeyEvent(ImGuiMod_Alt, keyReleased->alt);
        io.AddKeyEvent(ImGuiMod_Super, keyReleased->system);
    } else if (const auto* textEntered = event.getIf<sf::Event::TextEntered>()) {
        if (textEntered->unicode >= 32 && textEntered->unicode < 127) {
            io.AddInputCharacter(textEntered->unicode);
        }
    }
}

void ImGuiLayer::setDeltaTime(float deltaSeconds) {
    ImGui::GetIO().DeltaTime = deltaSeconds;
}

void ImGuiLayer::setDisplaySize(uint32_t width, uint32_t height) {
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
}

void ImGuiLayer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() {
    ImGui::Render();
}

void ImGuiLayer::renderDrawData(VkCommandBuffer commandBuffer) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void ImGuiLayer::initLuaBindings() {
    if (!L_) {
        spdlog::warn("Lua state is null. Skipping ImGui Lua bindings initialization.");
        return;
    }

    // ── ImVec2 constructor: ImVec2(x, y) → { x=..., y=... } ─────────────────
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        float x = static_cast<float>(luaL_optnumber(L, 1, 0.0));
        float y = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, y); lua_setfield(L, -2, "y");
        return 1;
    }, 0);
    lua_setglobal(L_, "ImVec2");

    // ── ImVec4 constructor: ImVec4(x, y, z, w) → { x=..., y=..., z=..., w=... }
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        float x = static_cast<float>(luaL_optnumber(L, 1, 0.0));
        float y = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        float z = static_cast<float>(luaL_optnumber(L, 3, 0.0));
        float w = static_cast<float>(luaL_optnumber(L, 4, 1.0));
        lua_createtable(L, 0, 4);
        lua_pushnumber(L, x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, z); lua_setfield(L, -2, "z");
        lua_pushnumber(L, w); lua_setfield(L, -2, "w");
        return 1;
    }, 0);
    lua_setglobal(L_, "ImVec4");

    // ── ImGui table ──────────────────────────────────────────────────────────
    lua_newtable(L_);
    int t = lua_gettop(L_);

    // ── Window ───────────────────────────────────────────────────────────────
    // Begin(name, [flags]) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* name = luaL_checkstring(L, 1);
        int flags        = static_cast<int>(luaL_optinteger(L, 2, 0));
        lua_pushboolean(L, ImGui::Begin(name, nullptr, flags));
        return 1;
    }, 0);
    lua_setfield(L_, t, "Begin");

    // End()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::End(); return 0; }, 0);
    lua_setfield(L_, t, "End");

    // BeginChild(id, [w, h, [flags]]) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* id = luaL_checkstring(L, 1);
        float w        = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        float h        = static_cast<float>(luaL_optnumber(L, 3, 0.0));
        int flags      = static_cast<int>(luaL_optinteger(L, 4, 0));
        lua_pushboolean(L, ImGui::BeginChild(id, ImVec2(w, h), ImGuiChildFlags_None, flags));
        return 1;
    }, 0);
    lua_setfield(L_, t, "BeginChild");

    // EndChild()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::EndChild(); return 0; }, 0);
    lua_setfield(L_, t, "EndChild");

    // SetNextWindowPos(x, y, [cond])
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        float x  = static_cast<float>(luaL_checknumber(L, 1));
        float y  = static_cast<float>(luaL_checknumber(L, 2));
        int cond = static_cast<int>(luaL_optinteger(L, 3, 0));
        ImGui::SetNextWindowPos(ImVec2(x, y), cond);
        return 0;
    }, 0);
    lua_setfield(L_, t, "SetNextWindowPos");

    // SetNextWindowSize(w, h, [cond])
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        float w  = static_cast<float>(luaL_checknumber(L, 1));
        float h  = static_cast<float>(luaL_checknumber(L, 2));
        int cond = static_cast<int>(luaL_optinteger(L, 3, 0));
        ImGui::SetNextWindowSize(ImVec2(w, h), cond);
        return 0;
    }, 0);
    lua_setfield(L_, t, "SetNextWindowSize");

    // SetNextWindowBgAlpha(alpha)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::SetNextWindowBgAlpha(static_cast<float>(luaL_checknumber(L, 1)));
        return 0;
    }, 0);
    lua_setfield(L_, t, "SetNextWindowBgAlpha");

    // ── Text ─────────────────────────────────────────────────────────────────
    // Text(str)  /  TextUnformatted(str)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::TextUnformatted(luaL_checkstring(L, 1));
        return 0;
    }, 0);
    lua_setfield(L_, t, "Text");

    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::TextUnformatted(luaL_checkstring(L, 1));
        return 0;
    }, 0);
    lua_setfield(L_, t, "TextUnformatted");

    // TextColored(r, g, b, a, str)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        float r          = static_cast<float>(luaL_checknumber(L, 1));
        float g          = static_cast<float>(luaL_checknumber(L, 2));
        float b          = static_cast<float>(luaL_checknumber(L, 3));
        float a          = static_cast<float>(luaL_optnumber(L, 4, 1.0));
        const char* text = luaL_checkstring(L, 5);
        ImGui::TextColored(ImVec4(r, g, b, a), "%s", text);
        return 0;
    }, 0);
    lua_setfield(L_, t, "TextColored");

    // TextDisabled(str)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::TextDisabled("%s", luaL_checkstring(L, 1));
        return 0;
    }, 0);
    lua_setfield(L_, t, "TextDisabled");

    // BulletText(str)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::BulletText("%s", luaL_checkstring(L, 1));
        return 0;
    }, 0);
    lua_setfield(L_, t, "BulletText");

    // LabelText(label, value)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::LabelText(luaL_checkstring(L, 1), "%s", luaL_checkstring(L, 2));
        return 0;
    }, 0);
    lua_setfield(L_, t, "LabelText");

    // ── Widgets ──────────────────────────────────────────────────────────────
    // Button(label, [w, h]) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* label = luaL_checkstring(L, 1);
        float w           = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        float h           = static_cast<float>(luaL_optnumber(L, 3, 0.0));
        lua_pushboolean(L, ImGui::Button(label, ImVec2(w, h)));
        return 1;
    }, 0);
    lua_setfield(L_, t, "Button");

    // SmallButton(label) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::SmallButton(luaL_checkstring(L, 1)));
        return 1;
    }, 0);
    lua_setfield(L_, t, "SmallButton");

    // Checkbox(label, v) → changed, v
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* label = luaL_checkstring(L, 1);
        bool v            = lua_toboolean(L, 2) != 0;
        bool changed      = ImGui::Checkbox(label, &v);
        lua_pushboolean(L, changed);
        lua_pushboolean(L, v);
        return 2;
    }, 0);
    lua_setfield(L_, t, "Checkbox");

    // RadioButton(label, active) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::RadioButton(luaL_checkstring(L, 1), lua_toboolean(L, 2) != 0));
        return 1;
    }, 0);
    lua_setfield(L_, t, "RadioButton");

    // ProgressBar(fraction, [w, h, [overlay]])  — w defaults to -1 (fill width)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        float fraction    = static_cast<float>(luaL_checknumber(L, 1));
        float w           = static_cast<float>(luaL_optnumber(L, 2, -1.0));
        float h           = static_cast<float>(luaL_optnumber(L, 3, 0.0));
        const char* label = luaL_optstring(L, 4, nullptr);
        ImGui::ProgressBar(fraction, ImVec2(w, h), label);
        return 0;
    }, 0);
    lua_setfield(L_, t, "ProgressBar");

    // SliderFloat(label, v, min, max) → changed, v
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* label = luaL_checkstring(L, 1);
        float v           = static_cast<float>(luaL_checknumber(L, 2));
        float vMin        = static_cast<float>(luaL_checknumber(L, 3));
        float vMax        = static_cast<float>(luaL_checknumber(L, 4));
        bool changed      = ImGui::SliderFloat(label, &v, vMin, vMax);
        lua_pushboolean(L, changed);
        lua_pushnumber(L, v);
        return 2;
    }, 0);
    lua_setfield(L_, t, "SliderFloat");

    // SliderInt(label, v, min, max) → changed, v
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* label = luaL_checkstring(L, 1);
        int v             = static_cast<int>(luaL_checkinteger(L, 2));
        int vMin          = static_cast<int>(luaL_checkinteger(L, 3));
        int vMax          = static_cast<int>(luaL_checkinteger(L, 4));
        bool changed      = ImGui::SliderInt(label, &v, vMin, vMax);
        lua_pushboolean(L, changed);
        lua_pushinteger(L, v);
        return 2;
    }, 0);
    lua_setfield(L_, t, "SliderInt");

    // DragFloat(label, v, [speed, min, max]) → changed, v
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* label = luaL_checkstring(L, 1);
        float v           = static_cast<float>(luaL_checknumber(L, 2));
        float speed       = static_cast<float>(luaL_optnumber(L, 3, 1.0));
        float vMin        = static_cast<float>(luaL_optnumber(L, 4, 0.0));
        float vMax        = static_cast<float>(luaL_optnumber(L, 5, 0.0));
        bool changed      = ImGui::DragFloat(label, &v, speed, vMin, vMax);
        lua_pushboolean(L, changed);
        lua_pushnumber(L, v);
        return 2;
    }, 0);
    lua_setfield(L_, t, "DragFloat");

    // DragInt(label, v, [speed, min, max]) → changed, v
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* label = luaL_checkstring(L, 1);
        int v             = static_cast<int>(luaL_checkinteger(L, 2));
        float speed       = static_cast<float>(luaL_optnumber(L, 3, 1.0));
        int vMin          = static_cast<int>(luaL_optinteger(L, 4, 0));
        int vMax          = static_cast<int>(luaL_optinteger(L, 5, 0));
        bool changed      = ImGui::DragInt(label, &v, speed, vMin, vMax);
        lua_pushboolean(L, changed);
        lua_pushinteger(L, v);
        return 2;
    }, 0);
    lua_setfield(L_, t, "DragInt");

    // InputFloat(label, v, [step, stepFast]) → changed, v
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* label = luaL_checkstring(L, 1);
        float v           = static_cast<float>(luaL_checknumber(L, 2));
        float step        = static_cast<float>(luaL_optnumber(L, 3, 0.0));
        float stepFast    = static_cast<float>(luaL_optnumber(L, 4, 0.0));
        bool changed      = ImGui::InputFloat(label, &v, step, stepFast);
        lua_pushboolean(L, changed);
        lua_pushnumber(L, v);
        return 2;
    }, 0);
    lua_setfield(L_, t, "InputFloat");

    // InputInt(label, v) → changed, v
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* label = luaL_checkstring(L, 1);
        int v             = static_cast<int>(luaL_checkinteger(L, 2));
        bool changed      = ImGui::InputInt(label, &v);
        lua_pushboolean(L, changed);
        lua_pushinteger(L, v);
        return 2;
    }, 0);
    lua_setfield(L_, t, "InputInt");

    // InputText(label, str) → changed, str   (max 1023 chars)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* label = luaL_checkstring(L, 1);
        const char* str   = luaL_checkstring(L, 2);
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", str);
        bool changed = ImGui::InputText(label, buf, sizeof(buf));
        lua_pushboolean(L, changed);
        lua_pushstring(L, buf);
        return 2;
    }, 0);
    lua_setfield(L_, t, "InputText");

    // ColorEdit4(label, r, g, b, [a]) → changed, r, g, b, a
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* label = luaL_checkstring(L, 1);
        float col[4]      = {
            static_cast<float>(luaL_checknumber(L, 2)),
            static_cast<float>(luaL_checknumber(L, 3)),
            static_cast<float>(luaL_checknumber(L, 4)),
            static_cast<float>(luaL_optnumber(L, 5, 1.0))
        };
        bool changed = ImGui::ColorEdit4(label, col);
        lua_pushboolean(L, changed);
        for (int i = 0; i < 4; ++i)
            lua_pushnumber(L, col[i]);
        return 5;
    }, 0);
    lua_setfield(L_, t, "ColorEdit4");

    // ── Layout ───────────────────────────────────────────────────────────────
    // Separator()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::Separator(); return 0; }, 0);
    lua_setfield(L_, t, "Separator");

    // Spacing()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::Spacing(); return 0; }, 0);
    lua_setfield(L_, t, "Spacing");

    // SameLine([offset, [spacing]])
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::SameLine(
            static_cast<float>(luaL_optnumber(L, 1, 0.0)),
            static_cast<float>(luaL_optnumber(L, 2, -1.0)));
        return 0;
    }, 0);
    lua_setfield(L_, t, "SameLine");

    // NewLine()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::NewLine(); return 0; }, 0);
    lua_setfield(L_, t, "NewLine");

    // Dummy(w, h)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::Dummy(ImVec2(
            static_cast<float>(luaL_checknumber(L, 1)),
            static_cast<float>(luaL_checknumber(L, 2))));
        return 0;
    }, 0);
    lua_setfield(L_, t, "Dummy");

    // Indent([w])
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::Indent(static_cast<float>(luaL_optnumber(L, 1, 0.0)));
        return 0;
    }, 0);
    lua_setfield(L_, t, "Indent");

    // Unindent([w])
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::Unindent(static_cast<float>(luaL_optnumber(L, 1, 0.0)));
        return 0;
    }, 0);
    lua_setfield(L_, t, "Unindent");

    // SetCursorPos(x, y)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::SetCursorPos(ImVec2(
            static_cast<float>(luaL_checknumber(L, 1)),
            static_cast<float>(luaL_checknumber(L, 2))));
        return 0;
    }, 0);
    lua_setfield(L_, t, "SetCursorPos");

    // GetCursorPos() → x, y
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImVec2 p = ImGui::GetCursorPos();
        lua_pushnumber(L, p.x);
        lua_pushnumber(L, p.y);
        return 2;
    }, 0);
    lua_setfield(L_, t, "GetCursorPos");

    // GetContentRegionAvail() → w, h
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImVec2 v = ImGui::GetContentRegionAvail();
        lua_pushnumber(L, v.x);
        lua_pushnumber(L, v.y);
        return 2;
    }, 0);
    lua_setfield(L_, t, "GetContentRegionAvail");

    // GetWindowSize() → w, h
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImVec2 v = ImGui::GetWindowSize();
        lua_pushnumber(L, v.x);
        lua_pushnumber(L, v.y);
        return 2;
    }, 0);
    lua_setfield(L_, t, "GetWindowSize");

    // GetWindowPos() → x, y
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImVec2 v = ImGui::GetWindowPos();
        lua_pushnumber(L, v.x);
        lua_pushnumber(L, v.y);
        return 2;
    }, 0);
    lua_setfield(L_, t, "GetWindowPos");

    // GetFrameHeight() → h
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushnumber(L, ImGui::GetFrameHeight());
        return 1;
    }, 0);
    lua_setfield(L_, t, "GetFrameHeight");

    // GetTextLineHeight() → h
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushnumber(L, ImGui::GetTextLineHeight());
        return 1;
    }, 0);
    lua_setfield(L_, t, "GetTextLineHeight");

    // GetDisplaySize() → w, h
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const ImVec2 sz = ImGui::GetIO().DisplaySize;
        lua_pushnumber(L, sz.x);
        lua_pushnumber(L, sz.y);
        return 2;
    }, 0);
    lua_setfield(L_, t, "GetDisplaySize");

    // ── Tree / Collapsing ─────────────────────────────────────────────────────
    // CollapsingHeader(label) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::CollapsingHeader(luaL_checkstring(L, 1)));
        return 1;
    }, 0);
    lua_setfield(L_, t, "CollapsingHeader");

    // TreeNode(label) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::TreeNode(luaL_checkstring(L, 1)));
        return 1;
    }, 0);
    lua_setfield(L_, t, "TreeNode");

    // TreePop()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::TreePop(); return 0; }, 0);
    lua_setfield(L_, t, "TreePop");

    // ── Tab bars ─────────────────────────────────────────────────────────────
    // BeginTabBar(id) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::BeginTabBar(luaL_checkstring(L, 1)));
        return 1;
    }, 0);
    lua_setfield(L_, t, "BeginTabBar");

    // EndTabBar()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::EndTabBar(); return 0; }, 0);
    lua_setfield(L_, t, "EndTabBar");

    // BeginTabItem(label) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::BeginTabItem(luaL_checkstring(L, 1)));
        return 1;
    }, 0);
    lua_setfield(L_, t, "BeginTabItem");

    // EndTabItem()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::EndTabItem(); return 0; }, 0);
    lua_setfield(L_, t, "EndTabItem");

    // ── Menus ────────────────────────────────────────────────────────────────
    // BeginMenuBar() → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::BeginMenuBar());
        return 1;
    }, 0);
    lua_setfield(L_, t, "BeginMenuBar");

    // EndMenuBar()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::EndMenuBar(); return 0; }, 0);
    lua_setfield(L_, t, "EndMenuBar");

    // BeginMenu(label) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::BeginMenu(luaL_checkstring(L, 1)));
        return 1;
    }, 0);
    lua_setfield(L_, t, "BeginMenu");

    // EndMenu()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::EndMenu(); return 0; }, 0);
    lua_setfield(L_, t, "EndMenu");

    // MenuItem(label, [shortcut, [selected]]) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        bool clicked = ImGui::MenuItem(
            luaL_checkstring(L, 1),
            luaL_optstring(L, 2, nullptr),
            lua_toboolean(L, 3) != 0);
        lua_pushboolean(L, clicked);
        return 1;
    }, 0);
    lua_setfield(L_, t, "MenuItem");

    // ── Popups ───────────────────────────────────────────────────────────────
    // OpenPopup(id)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::OpenPopup(luaL_checkstring(L, 1));
        return 0;
    }, 0);
    lua_setfield(L_, t, "OpenPopup");

    // BeginPopup(id) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::BeginPopup(luaL_checkstring(L, 1)));
        return 1;
    }, 0);
    lua_setfield(L_, t, "BeginPopup");

    // BeginPopupModal(name, [flags]) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::BeginPopupModal(
            luaL_checkstring(L, 1), nullptr,
            static_cast<ImGuiWindowFlags>(luaL_optinteger(L, 2, 0))));
        return 1;
    }, 0);
    lua_setfield(L_, t, "BeginPopupModal");

    // EndPopup()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::EndPopup(); return 0; }, 0);
    lua_setfield(L_, t, "EndPopup");

    // CloseCurrentPopup()
    lua_pushcclosure(L_, [](lua_State* L) -> int { ImGui::CloseCurrentPopup(); return 0; }, 0);
    lua_setfield(L_, t, "CloseCurrentPopup");

    // ── Style ────────────────────────────────────────────────────────────────
    // PushStyleColor(ImGuiCol, r, g, b, [a])
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::PushStyleColor(
            static_cast<ImGuiCol>(luaL_checkinteger(L, 1)),
            ImVec4(
                static_cast<float>(luaL_checknumber(L, 2)),
                static_cast<float>(luaL_checknumber(L, 3)),
                static_cast<float>(luaL_checknumber(L, 4)),
                static_cast<float>(luaL_optnumber(L, 5, 1.0))));
        return 0;
    }, 0);
    lua_setfield(L_, t, "PushStyleColor");

    // PopStyleColor([count])
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::PopStyleColor(static_cast<int>(luaL_optinteger(L, 1, 1)));
        return 0;
    }, 0);
    lua_setfield(L_, t, "PopStyleColor");

    // PushStyleVar(ImGuiStyleVar, value_or_x, [y])  — pass y for Vec2 vars
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        auto idx = static_cast<ImGuiStyleVar>(luaL_checkinteger(L, 1));
        if (lua_type(L, 3) == LUA_TNUMBER) {
            ImGui::PushStyleVar(idx, ImVec2(
                static_cast<float>(luaL_checknumber(L, 2)),
                static_cast<float>(luaL_checknumber(L, 3))));
        } else {
            ImGui::PushStyleVar(idx, static_cast<float>(luaL_checknumber(L, 2)));
        }
        return 0;
    }, 0);
    lua_setfield(L_, t, "PushStyleVar");

    // PopStyleVar([count])
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::PopStyleVar(static_cast<int>(luaL_optinteger(L, 1, 1)));
        return 0;
    }, 0);
    lua_setfield(L_, t, "PopStyleVar");

    // ── Utilities ────────────────────────────────────────────────────────────
    // IsItemHovered() → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::IsItemHovered());
        return 1;
    }, 0);
    lua_setfield(L_, t, "IsItemHovered");

    // IsItemClicked([button]) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::IsItemClicked(
            static_cast<ImGuiMouseButton>(luaL_optinteger(L, 1, 0))));
        return 1;
    }, 0);
    lua_setfield(L_, t, "IsItemClicked");

    // IsWindowFocused() → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::IsWindowFocused());
        return 1;
    }, 0);
    lua_setfield(L_, t, "IsWindowFocused");

    // IsWindowHovered() → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, ImGui::IsWindowHovered());
        return 1;
    }, 0);
    lua_setfield(L_, t, "IsWindowHovered");

    // SetTooltip(text)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::SetTooltip("%s", luaL_checkstring(L, 1));
        return 0;
    }, 0);
    lua_setfield(L_, t, "SetTooltip");

    // ── Image / ImageButton ───────────────────────────────────────────────────
    // Image(texture, w, h)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        auto** ppTex = static_cast<VulkanTexture**>(luaL_checkudata(L, 1, "NST.Texture"));
        float w      = static_cast<float>(luaL_checknumber(L, 2));
        float h      = static_cast<float>(luaL_checknumber(L, 3));
        ImGui::Image((*ppTex)->textureRef(), ImVec2(w, h));
        return 0;
    }, 0);
    lua_setfield(L_, t, "Image");

    // ImageButton(id, texture, w, h) → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const char* id = luaL_checkstring(L, 1);
        auto** ppTex   = static_cast<VulkanTexture**>(luaL_checkudata(L, 2, "NST.Texture"));
        float w        = static_cast<float>(luaL_checknumber(L, 3));
        float h        = static_cast<float>(luaL_checknumber(L, 4));
        lua_pushboolean(L, ImGui::ImageButton(id, (*ppTex)->textureRef(), ImVec2(w, h)));
        return 1;
    }, 0);
    lua_setfield(L_, t, "ImageButton");

    // ── Font / viewport helpers ────────────────────────────────────────────────
    // SetNextWindowFullscreen()  — sets pos/size/viewport to main viewport
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::SetNextWindowViewport(vp->ID);
        return 0;
    }, 0);
    lua_setfield(L_, t, "SetNextWindowFullscreen");

    // PushFont(font_lightuserdata)
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        if (lua_type(L, 1) == LUA_TLIGHTUSERDATA) {
            ImGui::PushFont(static_cast<ImFont*>(lua_touserdata(L, 1)));
        } else {
            ImGui::PushFont(nullptr);
        }
        return 0;
    }, 0);
    lua_setfield(L_, t, "PushFont");

    // PopFont()
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImGui::PopFont();
        return 0;
    }, 0);
    lua_setfield(L_, t, "PopFont");

    // CalcTextSize(text) → w, h
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        ImVec2 sz = ImGui::CalcTextSize(luaL_checkstring(L, 1));
        lua_pushnumber(L, sz.x);
        lua_pushnumber(L, sz.y);
        return 2;
    }, 0);
    lua_setfield(L_, t, "CalcTextSize");

    // GetWindowWidth() → number
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushnumber(L, ImGui::GetWindowWidth());
        return 1;
    }, 0);
    lua_setfield(L_, t, "GetWindowWidth");

    // GetWindowHeight() → number
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        lua_pushnumber(L, ImGui::GetWindowHeight());
        return 1;
    }, 0);
    lua_setfield(L_, t, "GetWindowHeight");

    lua_setglobal(L_, "ImGui");

    // ── ImGuiWindowFlags constants ───────────────────────────────────────────
    lua_newtable(L_);
    lua_pushinteger(L_, ImGuiWindowFlags_None);              lua_setfield(L_, -2, "None");
    lua_pushinteger(L_, ImGuiWindowFlags_NoTitleBar);        lua_setfield(L_, -2, "NoTitleBar");
    lua_pushinteger(L_, ImGuiWindowFlags_NoResize);          lua_setfield(L_, -2, "NoResize");
    lua_pushinteger(L_, ImGuiWindowFlags_NoMove);            lua_setfield(L_, -2, "NoMove");
    lua_pushinteger(L_, ImGuiWindowFlags_NoScrollbar);       lua_setfield(L_, -2, "NoScrollbar");
    lua_pushinteger(L_, ImGuiWindowFlags_NoScrollWithMouse); lua_setfield(L_, -2, "NoScrollWithMouse");
    lua_pushinteger(L_, ImGuiWindowFlags_NoCollapse);        lua_setfield(L_, -2, "NoCollapse");
    lua_pushinteger(L_, ImGuiWindowFlags_AlwaysAutoResize);  lua_setfield(L_, -2, "AlwaysAutoResize");
    lua_pushinteger(L_, ImGuiWindowFlags_NoBackground);      lua_setfield(L_, -2, "NoBackground");
    lua_pushinteger(L_, ImGuiWindowFlags_NoSavedSettings);   lua_setfield(L_, -2, "NoSavedSettings");
    lua_pushinteger(L_, ImGuiWindowFlags_MenuBar);           lua_setfield(L_, -2, "MenuBar");
    lua_pushinteger(L_, ImGuiWindowFlags_NoNav);             lua_setfield(L_, -2, "NoNav");
    lua_pushinteger(L_, ImGuiWindowFlags_NoDecoration);          lua_setfield(L_, -2, "NoDecoration");
    lua_pushinteger(L_, ImGuiWindowFlags_NoBringToFrontOnFocus);  lua_setfield(L_, -2, "NoBringToFrontOnFocus");
    lua_setglobal(L_, "ImGuiWindowFlags");

    // ── ImGuiCond constants ──────────────────────────────────────────────────
    lua_newtable(L_);
    lua_pushinteger(L_, ImGuiCond_None);         lua_setfield(L_, -2, "None");
    lua_pushinteger(L_, ImGuiCond_Always);       lua_setfield(L_, -2, "Always");
    lua_pushinteger(L_, ImGuiCond_Once);         lua_setfield(L_, -2, "Once");
    lua_pushinteger(L_, ImGuiCond_FirstUseEver); lua_setfield(L_, -2, "FirstUseEver");
    lua_pushinteger(L_, ImGuiCond_Appearing);    lua_setfield(L_, -2, "Appearing");
    lua_setglobal(L_, "ImGuiCond");

    // ── ImGuiCol constants ───────────────────────────────────────────────────
    lua_newtable(L_);
    lua_pushinteger(L_, ImGuiCol_Text);             lua_setfield(L_, -2, "Text");
    lua_pushinteger(L_, ImGuiCol_TextDisabled);     lua_setfield(L_, -2, "TextDisabled");
    lua_pushinteger(L_, ImGuiCol_WindowBg);         lua_setfield(L_, -2, "WindowBg");
    lua_pushinteger(L_, ImGuiCol_ChildBg);          lua_setfield(L_, -2, "ChildBg");
    lua_pushinteger(L_, ImGuiCol_PopupBg);          lua_setfield(L_, -2, "PopupBg");
    lua_pushinteger(L_, ImGuiCol_Border);           lua_setfield(L_, -2, "Border");
    lua_pushinteger(L_, ImGuiCol_FrameBg);          lua_setfield(L_, -2, "FrameBg");
    lua_pushinteger(L_, ImGuiCol_FrameBgHovered);   lua_setfield(L_, -2, "FrameBgHovered");
    lua_pushinteger(L_, ImGuiCol_FrameBgActive);    lua_setfield(L_, -2, "FrameBgActive");
    lua_pushinteger(L_, ImGuiCol_TitleBg);          lua_setfield(L_, -2, "TitleBg");
    lua_pushinteger(L_, ImGuiCol_TitleBgActive);    lua_setfield(L_, -2, "TitleBgActive");
    lua_pushinteger(L_, ImGuiCol_Button);           lua_setfield(L_, -2, "Button");
    lua_pushinteger(L_, ImGuiCol_ButtonHovered);    lua_setfield(L_, -2, "ButtonHovered");
    lua_pushinteger(L_, ImGuiCol_ButtonActive);     lua_setfield(L_, -2, "ButtonActive");
    lua_pushinteger(L_, ImGuiCol_Header);           lua_setfield(L_, -2, "Header");
    lua_pushinteger(L_, ImGuiCol_HeaderHovered);    lua_setfield(L_, -2, "HeaderHovered");
    lua_pushinteger(L_, ImGuiCol_HeaderActive);     lua_setfield(L_, -2, "HeaderActive");
    lua_pushinteger(L_, ImGuiCol_Separator);        lua_setfield(L_, -2, "Separator");
    lua_pushinteger(L_, ImGuiCol_CheckMark);        lua_setfield(L_, -2, "CheckMark");
    lua_pushinteger(L_, ImGuiCol_SliderGrab);       lua_setfield(L_, -2, "SliderGrab");
    lua_pushinteger(L_, ImGuiCol_SliderGrabActive); lua_setfield(L_, -2, "SliderGrabActive");
    lua_setglobal(L_, "ImGuiCol");

    // ── ImGuiStyleVar constants ──────────────────────────────────────────────
    lua_newtable(L_);
    lua_pushinteger(L_, ImGuiStyleVar_Alpha);             lua_setfield(L_, -2, "Alpha");
    lua_pushinteger(L_, ImGuiStyleVar_WindowPadding);     lua_setfield(L_, -2, "WindowPadding");
    lua_pushinteger(L_, ImGuiStyleVar_WindowRounding);    lua_setfield(L_, -2, "WindowRounding");
    lua_pushinteger(L_, ImGuiStyleVar_WindowBorderSize);  lua_setfield(L_, -2, "WindowBorderSize");
    lua_pushinteger(L_, ImGuiStyleVar_FramePadding);      lua_setfield(L_, -2, "FramePadding");
    lua_pushinteger(L_, ImGuiStyleVar_FrameRounding);     lua_setfield(L_, -2, "FrameRounding");
    lua_pushinteger(L_, ImGuiStyleVar_ItemSpacing);       lua_setfield(L_, -2, "ItemSpacing");
    lua_pushinteger(L_, ImGuiStyleVar_ItemInnerSpacing);  lua_setfield(L_, -2, "ItemInnerSpacing");
    lua_pushinteger(L_, ImGuiStyleVar_IndentSpacing);     lua_setfield(L_, -2, "IndentSpacing");
    lua_pushinteger(L_, ImGuiStyleVar_ScrollbarSize);     lua_setfield(L_, -2, "ScrollbarSize");
    lua_pushinteger(L_, ImGuiStyleVar_GrabMinSize);       lua_setfield(L_, -2, "GrabMinSize");
    lua_pushinteger(L_, ImGuiStyleVar_GrabRounding);      lua_setfield(L_, -2, "GrabRounding");
    lua_setglobal(L_, "ImGuiStyleVar");

    // ── Texture userdata ("NST.Texture") ─────────────────────────────────────
    // Full userdata holds a heap-allocated VulkanTexture*; __gc deletes it.
    luaL_newmetatable(L_, "NST.Texture");
    int texMeta = lua_gettop(L_);

    lua_pushcclosure(L_, [](lua_State* L) -> int {
        auto** ppTex = static_cast<VulkanTexture**>(lua_touserdata(L, 1));
        if (ppTex && *ppTex) {
            delete *ppTex;
            *ppTex = nullptr;
        }
        return 0;
    }, 0);
    lua_setfield(L_, texMeta, "__gc");

    lua_newtable(L_);
    int texMethods = lua_gettop(L_);

    // texture:width() → number
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        auto** ppTex = static_cast<VulkanTexture**>(luaL_checkudata(L, 1, "NST.Texture"));
        lua_pushnumber(L, (*ppTex)->width());
        return 1;
    }, 0);
    lua_setfield(L_, texMethods, "width");

    // texture:height() → number
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        auto** ppTex = static_cast<VulkanTexture**>(luaL_checkudata(L, 1, "NST.Texture"));
        lua_pushnumber(L, (*ppTex)->height());
        return 1;
    }, 0);
    lua_setfield(L_, texMethods, "height");

    // texture:isValid() → bool
    lua_pushcclosure(L_, [](lua_State* L) -> int {
        auto** ppTex = static_cast<VulkanTexture**>(luaL_checkudata(L, 1, "NST.Texture"));
        lua_pushboolean(L, (*ppTex)->isValid());
        return 1;
    }, 0);
    lua_setfield(L_, texMethods, "isValid");

    lua_setfield(L_, texMeta, "__index"); // metatable.__index = methods table
    lua_pop(L_, 1);                        // pop metatable

    // ── Texture global: Texture.load(VulkanContext, path) → texture | nil, err
    lua_newtable(L_);
    int texTable = lua_gettop(L_);

    lua_pushcclosure(L_, [](lua_State* L) -> int {
        if (lua_type(L, 1) != LUA_TLIGHTUSERDATA) {
            return luaL_error(L, "Texture.load: arg 1 must be VulkanContext lightuserdata");
        }
        auto* ctx        = static_cast<VulkanContext*>(lua_touserdata(L, 1));
        const char* path = luaL_checkstring(L, 2);

        auto* tex = new VulkanTexture(ctx->device(), ctx->allocator(), ctx->commandPool(), ctx->graphicsQueue());
        if (!tex->loadFromFile(path)) {
            delete tex;
            lua_pushnil(L);
            lua_pushfstring(L, "Texture.load: failed to load '%s'", path);
            return 2;
        }

        auto** ppTex = static_cast<VulkanTexture**>(lua_newuserdata(L, sizeof(VulkanTexture*)));
        *ppTex = tex;
        luaL_getmetatable(L, "NST.Texture");
        lua_setmetatable(L, -2);
        return 1;
    }, 0);
    lua_setfield(L_, texTable, "load");

    lua_setglobal(L_, "Texture");
}

void ImGuiLayer::applyModernStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 12.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.ScrollbarRounding = 8.0f;
    style.TabRounding = 8.0f;

    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;

    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.97f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.58f, 0.63f, 1.0f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.10f, 0.13f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.14f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.24f, 0.30f, 0.70f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.19f, 0.24f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.22f, 0.28f, 1.0f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.10f, 0.12f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.12f, 1.0f);

    colors[ImGuiCol_Button] = ImVec4(0.13f, 0.44f, 0.57f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.52f, 0.67f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.10f, 0.37f, 0.48f, 1.0f);

    colors[ImGuiCol_Header] = ImVec4(0.13f, 0.44f, 0.57f, 0.55f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.52f, 0.67f, 0.70f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.10f, 0.37f, 0.48f, 0.90f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.43f, 0.80f, 0.90f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.76f, 0.86f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.29f, 0.64f, 0.74f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.28f, 0.33f, 0.90f);
}

void ImGuiLayer::loadUiFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const std::array<const char*, 4> regularCandidates = {"assets/fonts/Inter-Regular.ttf", "assets/fonts/SegoeUI.ttf",
                                                          "assets/fonts/Roboto-Regular.ttf", "assets/fonts/times.ttf"};
    const std::array<const char*, 3> headingCandidates = {
        "assets/fonts/Inter-Bold.ttf", "assets/fonts/Inter-SemiBold.ttf", "assets/fonts/timesbd.ttf"};

    ImFont* regularFont = nullptr;
    for (const char* candidate : regularCandidates) {
        if (std::filesystem::exists(candidate)) {
            regularFont = io.Fonts->AddFontFromFileTTF(candidate, 18.0f);
            if (regularFont != nullptr) {
                break;
            }
        }
    }

    for (const char* candidate : headingCandidates) {
        if (std::filesystem::exists(candidate)) {
            headingFont_ = io.Fonts->AddFontFromFileTTF(candidate, 26.0f);
            if (headingFont_ != nullptr) {
                break;
            }
        }
    }

    for (const char* candidate : headingCandidates) {
        if (std::filesystem::exists(candidate)) {
            titleFont_ = io.Fonts->AddFontFromFileTTF(candidate, 52.0f);
            if (titleFont_ != nullptr) {
                break;
            }
        }
    }

    if (regularFont == nullptr) {
        regularFont = io.Fonts->AddFontDefault();
    }
    if (headingFont_ == nullptr) {
        headingFont_ = regularFont;
    }

    io.FontDefault = regularFont;
}
