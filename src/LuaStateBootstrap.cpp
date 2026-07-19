#include "LuaStateBootstrap.hpp"

#include "VulkanContext.hpp"
#include "lua.hpp"
#include "utility/VulkanTexture.hpp"

#include <imgui.h>

#include <spdlog/spdlog.h>

namespace LuaStateBootstrap {
void initializeEngineState(lua_State* L, const VulkanContext* context) {
    if (!L) {
        spdlog::warn("Lua state is null. Skipping ImGui Lua bindings initialization.");
        return;
    }

    if (context) {
        lua_pushlightuserdata(L, const_cast<VulkanContext*>(context));
    } else {
        lua_pushnil(L);
    }
    lua_setglobal(L, "VulkanContext");

    // ImVec2 constructor: ImVec2(x, y) -> { x=..., y=... }
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            float x = static_cast<float>(luaL_optnumber(L, 1, 0.0));
            float y = static_cast<float>(luaL_optnumber(L, 2, 0.0));
            lua_createtable(L, 0, 2);
            lua_pushnumber(L, x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, y);
            lua_setfield(L, -2, "y");
            return 1;
        },
        0);
    lua_setglobal(L, "ImVec2");

    // ImVec4 constructor: ImVec4(x, y, z, w) -> { x=..., y=..., z=..., w=... }
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            float x = static_cast<float>(luaL_optnumber(L, 1, 0.0));
            float y = static_cast<float>(luaL_optnumber(L, 2, 0.0));
            float z = static_cast<float>(luaL_optnumber(L, 3, 0.0));
            float w = static_cast<float>(luaL_optnumber(L, 4, 1.0));
            lua_createtable(L, 0, 4);
            lua_pushnumber(L, x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, z);
            lua_setfield(L, -2, "z");
            lua_pushnumber(L, w);
            lua_setfield(L, -2, "w");
            return 1;
        },
        0);
    lua_setglobal(L, "ImVec4");

    // ImGui table
    lua_newtable(L);
    int t = lua_gettop(L);

    // Window
    // Begin(name, [flags]) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* name = luaL_checkstring(L, 1);
            int flags = static_cast<int>(luaL_optinteger(L, 2, 0));
            lua_pushboolean(L, ImGui::Begin(name, nullptr, flags));
            return 1;
        },
        0);
    lua_setfield(L, t, "Begin");

    // End()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::End();
            return 0;
        },
        0);
    lua_setfield(L, t, "End");

    // BeginChild(id, [w, h, [flags]]) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* id = luaL_checkstring(L, 1);
            float w = static_cast<float>(luaL_optnumber(L, 2, 0.0));
            float h = static_cast<float>(luaL_optnumber(L, 3, 0.0));
            int flags = static_cast<int>(luaL_optinteger(L, 4, 0));
            lua_pushboolean(L, ImGui::BeginChild(id, ImVec2(w, h), ImGuiChildFlags_None, flags));
            return 1;
        },
        0);
    lua_setfield(L, t, "BeginChild");

    // EndChild()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::EndChild();
            return 0;
        },
        0);
    lua_setfield(L, t, "EndChild");

    // BeginGroup()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::BeginGroup();
            return 0;
        },
        0);
    lua_setfield(L, t, "BeginGroup");

    // EndGroup()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::EndGroup();
            return 0;
        },
        0);
    lua_setfield(L, t, "EndGroup");

    // SetNextWindowPos(x, y, [cond])
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            float x = static_cast<float>(luaL_checknumber(L, 1));
            float y = static_cast<float>(luaL_checknumber(L, 2));
            int cond = static_cast<int>(luaL_optinteger(L, 3, 0));
            ImGui::SetNextWindowPos(ImVec2(x, y), cond);
            return 0;
        },
        0);
    lua_setfield(L, t, "SetNextWindowPos");

    // SetNextWindowSize(w, h, [cond])
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            float w = static_cast<float>(luaL_checknumber(L, 1));
            float h = static_cast<float>(luaL_checknumber(L, 2));
            int cond = static_cast<int>(luaL_optinteger(L, 3, 0));
            ImGui::SetNextWindowSize(ImVec2(w, h), cond);
            return 0;
        },
        0);
    lua_setfield(L, t, "SetNextWindowSize");

    // SetNextWindowBgAlpha(alpha)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::SetNextWindowBgAlpha(static_cast<float>(luaL_checknumber(L, 1)));
            return 0;
        },
        0);
    lua_setfield(L, t, "SetNextWindowBgAlpha");

    // Text
    // Text(str) / TextUnformatted(str)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::TextUnformatted(luaL_checkstring(L, 1));
            return 0;
        },
        0);
    lua_setfield(L, t, "Text");

    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            auto const str = luaL_checkstring(L, 1);
            ImGui::TextWrapped(str);
            return 0;
        },
        0);
    lua_setfield(L, t, "TextWrapped");    

    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::TextUnformatted(luaL_checkstring(L, 1));
            return 0;
        },
        0);
    lua_setfield(L, t, "TextUnformatted");

    // TextColored(r, g, b, a, str)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            float r = static_cast<float>(luaL_checknumber(L, 1));
            float g = static_cast<float>(luaL_checknumber(L, 2));
            float b = static_cast<float>(luaL_checknumber(L, 3));
            float a = static_cast<float>(luaL_optnumber(L, 4, 1.0));
            const char* text = luaL_checkstring(L, 5);
            ImGui::TextColored(ImVec4(r, g, b, a), "%s", text);
            return 0;
        },
        0);
    lua_setfield(L, t, "TextColored");

    // TextDisabled(str)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::TextDisabled("%s", luaL_checkstring(L, 1));
            return 0;
        },
        0);
    lua_setfield(L, t, "TextDisabled");

    // BulletText(str)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::BulletText("%s", luaL_checkstring(L, 1));
            return 0;
        },
        0);
    lua_setfield(L, t, "BulletText");

    // LabelText(label, value)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::LabelText(luaL_checkstring(L, 1), "%s", luaL_checkstring(L, 2));
            return 0;
        },
        0);
    lua_setfield(L, t, "LabelText");

    // Widgets
    // Button(label, [w, h]) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* label = luaL_checkstring(L, 1);
            float w = static_cast<float>(luaL_optnumber(L, 2, 0.0));
            float h = static_cast<float>(luaL_optnumber(L, 3, 0.0));
            lua_pushboolean(L, ImGui::Button(label, ImVec2(w, h)));
            return 1;
        },
        0);
    lua_setfield(L, t, "Button");

    // SmallButton(label) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::SmallButton(luaL_checkstring(L, 1)));
            return 1;
        },
        0);
    lua_setfield(L, t, "SmallButton");

    lua_pushcclosure(L, [](lua_State* L) -> int {
        const char* label = luaL_checkstring(L, 1);
        bool v = lua_toboolean(L, 2) != 0;
        bool changed = ImGui::Selectable(label, &v);
        lua_pushboolean(L, changed);
        lua_pushboolean(L, v);
        return 2;
    }, 0);
    lua_setfield(L, t, "Selectable");

    // Checkbox(label, v) -> changed, v
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* label = luaL_checkstring(L, 1);
            bool v = lua_toboolean(L, 2) != 0;
            bool changed = ImGui::Checkbox(label, &v);
            lua_pushboolean(L, changed);
            lua_pushboolean(L, v);
            return 2;
        },
        0);
    lua_setfield(L, t, "Checkbox");

    // RadioButton(label, active) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::RadioButton(luaL_checkstring(L, 1), lua_toboolean(L, 2) != 0));
            return 1;
        },
        0);
    lua_setfield(L, t, "RadioButton");

    // ProgressBar(fraction, [w, h, [overlay]]) -> w defaults to -1 (fill width)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            float fraction = static_cast<float>(luaL_checknumber(L, 1));
            float w = static_cast<float>(luaL_optnumber(L, 2, -1.0));
            float h = static_cast<float>(luaL_optnumber(L, 3, 0.0));
            const char* label = luaL_optstring(L, 4, nullptr);
            ImGui::ProgressBar(fraction, ImVec2(w, h), label);
            return 0;
        },
        0);
    lua_setfield(L, t, "ProgressBar");

    // SliderFloat(label, v, min, max) -> changed, v
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* label = luaL_checkstring(L, 1);
            float v = static_cast<float>(luaL_checknumber(L, 2));
            float vMin = static_cast<float>(luaL_checknumber(L, 3));
            float vMax = static_cast<float>(luaL_checknumber(L, 4));
            bool changed = ImGui::SliderFloat(label, &v, vMin, vMax);
            lua_pushboolean(L, changed);
            lua_pushnumber(L, v);
            return 2;
        },
        0);
    lua_setfield(L, t, "SliderFloat");

    // SliderInt(label, v, min, max) -> changed, v
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* label = luaL_checkstring(L, 1);
            int v = static_cast<int>(luaL_checkinteger(L, 2));
            int vMin = static_cast<int>(luaL_checkinteger(L, 3));
            int vMax = static_cast<int>(luaL_checkinteger(L, 4));
            bool changed = ImGui::SliderInt(label, &v, vMin, vMax);
            lua_pushboolean(L, changed);
            lua_pushinteger(L, v);
            return 2;
        },
        0);
    lua_setfield(L, t, "SliderInt");

    // DragFloat(label, v, [speed, min, max]) -> changed, v
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* label = luaL_checkstring(L, 1);
            float v = static_cast<float>(luaL_checknumber(L, 2));
            float speed = static_cast<float>(luaL_optnumber(L, 3, 1.0));
            float vMin = static_cast<float>(luaL_optnumber(L, 4, 0.0));
            float vMax = static_cast<float>(luaL_optnumber(L, 5, 0.0));
            bool changed = ImGui::DragFloat(label, &v, speed, vMin, vMax);
            lua_pushboolean(L, changed);
            lua_pushnumber(L, v);
            return 2;
        },
        0);
    lua_setfield(L, t, "DragFloat");

    // DragInt(label, v, [speed, min, max]) -> changed, v
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* label = luaL_checkstring(L, 1);
            int v = static_cast<int>(luaL_checkinteger(L, 2));
            float speed = static_cast<float>(luaL_optnumber(L, 3, 1.0));
            int vMin = static_cast<int>(luaL_optinteger(L, 4, 0));
            int vMax = static_cast<int>(luaL_optinteger(L, 5, 0));
            bool changed = ImGui::DragInt(label, &v, speed, vMin, vMax);
            lua_pushboolean(L, changed);
            lua_pushinteger(L, v);
            return 2;
        },
        0);
    lua_setfield(L, t, "DragInt");

    // InputFloat(label, v, [step, stepFast]) -> changed, v
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* label = luaL_checkstring(L, 1);
            float v = static_cast<float>(luaL_checknumber(L, 2));
            float step = static_cast<float>(luaL_optnumber(L, 3, 0.0));
            float stepFast = static_cast<float>(luaL_optnumber(L, 4, 0.0));
            bool changed = ImGui::InputFloat(label, &v, step, stepFast);
            lua_pushboolean(L, changed);
            lua_pushnumber(L, v);
            return 2;
        },
        0);
    lua_setfield(L, t, "InputFloat");

    // InputInt(label, v) -> changed, v
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* label = luaL_checkstring(L, 1);
            int v = static_cast<int>(luaL_checkinteger(L, 2));
            bool changed = ImGui::InputInt(label, &v);
            lua_pushboolean(L, changed);
            lua_pushinteger(L, v);
            return 2;
        },
        0);
    lua_setfield(L, t, "InputInt");

    // InputText(label, str) -> changed, str (max 1023 chars)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* label = luaL_checkstring(L, 1);
            const char* str = luaL_checkstring(L, 2);
            char buf[1024];
            snprintf(buf, sizeof(buf), "%s", str);
            bool changed = ImGui::InputText(label, buf, sizeof(buf));
            lua_pushboolean(L, changed);
            lua_pushstring(L, buf);
            return 2;
        },
        0);
    lua_setfield(L, t, "InputText");

    // ColorEdit4(label, r, g, b, [a]) -> changed, r, g, b, a
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* label = luaL_checkstring(L, 1);
            float col[4] = {static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3)),
                            static_cast<float>(luaL_checknumber(L, 4)), static_cast<float>(luaL_optnumber(L, 5, 1.0))};
            bool changed = ImGui::ColorEdit4(label, col);
            lua_pushboolean(L, changed);
            for (int i = 0; i < 4; ++i) {
                lua_pushnumber(L, col[i]);
            }
            return 5;
        },
        0);
    lua_setfield(L, t, "ColorEdit4");

    // Layout
    // Separator()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::Separator();
            return 0;
        },
        0);
    lua_setfield(L, t, "Separator");

    // Spacing()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::Spacing();
            return 0;
        },
        0);
    lua_setfield(L, t, "Spacing");

    // SameLine([offset, [spacing]])
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::SameLine(static_cast<float>(luaL_optnumber(L, 1, 0.0)),
                            static_cast<float>(luaL_optnumber(L, 2, -1.0)));
            return 0;
        },
        0);
    lua_setfield(L, t, "SameLine");

    // NewLine()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::NewLine();
            return 0;
        },
        0);
    lua_setfield(L, t, "NewLine");

    // Dummy(w, h)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::Dummy(
                ImVec2(static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2))));
            return 0;
        },
        0);
    lua_setfield(L, t, "Dummy");

    // Indent([w])
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::Indent(static_cast<float>(luaL_optnumber(L, 1, 0.0)));
            return 0;
        },
        0);
    lua_setfield(L, t, "Indent");

    // Unindent([w])
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::Unindent(static_cast<float>(luaL_optnumber(L, 1, 0.0)));
            return 0;
        },
        0);
    lua_setfield(L, t, "Unindent");

    // SetCursorPos(x, y)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::SetCursorPos(
                ImVec2(static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2))));
            return 0;
        },
        0);
    lua_setfield(L, t, "SetCursorPos");

    // GetCursorPos() -> x, y
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImVec2 p = ImGui::GetCursorPos();
            lua_pushnumber(L, p.x);
            lua_pushnumber(L, p.y);
            return 2;
        },
        0);
    lua_setfield(L, t, "GetCursorPos");

    // GetContentRegionAvail() -> w, h
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImVec2 v = ImGui::GetContentRegionAvail();
            lua_pushnumber(L, v.x);
            lua_pushnumber(L, v.y);
            return 2;
        },
        0);
    lua_setfield(L, t, "GetContentRegionAvail");

    // GetWindowSize() -> w, h
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImVec2 v = ImGui::GetWindowSize();
            lua_pushnumber(L, v.x);
            lua_pushnumber(L, v.y);
            return 2;
        },
        0);
    lua_setfield(L, t, "GetWindowSize");

    // GetWindowPos() -> x, y
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImVec2 v = ImGui::GetWindowPos();
            lua_pushnumber(L, v.x);
            lua_pushnumber(L, v.y);
            return 2;
        },
        0);
    lua_setfield(L, t, "GetWindowPos");

    // GetFrameHeight() -> h
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushnumber(L, ImGui::GetFrameHeight());
            return 1;
        },
        0);
    lua_setfield(L, t, "GetFrameHeight");

    // GetTextLineHeight() -> h
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushnumber(L, ImGui::GetTextLineHeight());
            return 1;
        },
        0);
    lua_setfield(L, t, "GetTextLineHeight");

    // GetDisplaySize() -> w, h
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const ImVec2 sz = ImGui::GetIO().DisplaySize;
            lua_pushnumber(L, sz.x);
            lua_pushnumber(L, sz.y);
            return 2;
        },
        0);
    lua_setfield(L, t, "GetDisplaySize");

    // Tree / Collapsing
    // CollapsingHeader(label) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::CollapsingHeader(luaL_checkstring(L, 1)));
            return 1;
        },
        0);
    lua_setfield(L, t, "CollapsingHeader");

    // TreeNode(label) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::TreeNode(luaL_checkstring(L, 1)));
            return 1;
        },
        0);
    lua_setfield(L, t, "TreeNode");

    // TreePop()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::TreePop();
            return 0;
        },
        0);
    lua_setfield(L, t, "TreePop");

    // Tab bars
    // BeginTabBar(id) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::BeginTabBar(luaL_checkstring(L, 1)));
            return 1;
        },
        0);
    lua_setfield(L, t, "BeginTabBar");

    // EndTabBar()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::EndTabBar();
            return 0;
        },
        0);
    lua_setfield(L, t, "EndTabBar");

    // BeginTabItem(label) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::BeginTabItem(luaL_checkstring(L, 1)));
            return 1;
        },
        0);
    lua_setfield(L, t, "BeginTabItem");

    // EndTabItem()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::EndTabItem();
            return 0;
        },
        0);
    lua_setfield(L, t, "EndTabItem");

    // Menus
    // BeginMenuBar() -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::BeginMenuBar());
            return 1;
        },
        0);
    lua_setfield(L, t, "BeginMenuBar");

    // EndMenuBar()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::EndMenuBar();
            return 0;
        },
        0);
    lua_setfield(L, t, "EndMenuBar");

    // BeginMenu(label) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::BeginMenu(luaL_checkstring(L, 1)));
            return 1;
        },
        0);
    lua_setfield(L, t, "BeginMenu");

    // EndMenu()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::EndMenu();
            return 0;
        },
        0);
    lua_setfield(L, t, "EndMenu");

    // MenuItem(label, [shortcut, [selected]]) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            bool clicked =
                ImGui::MenuItem(luaL_checkstring(L, 1), luaL_optstring(L, 2, nullptr), lua_toboolean(L, 3) != 0);
            lua_pushboolean(L, clicked);
            return 1;
        },
        0);
    lua_setfield(L, t, "MenuItem");

    // Popups
    // OpenPopup(id)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::OpenPopup(luaL_checkstring(L, 1));
            return 0;
        },
        0);
    lua_setfield(L, t, "OpenPopup");

    // BeginPopup(id) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::BeginPopup(luaL_checkstring(L, 1)));
            return 1;
        },
        0);
    lua_setfield(L, t, "BeginPopup");

    // BeginPopupModal(name, [flags]) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::BeginPopupModal(luaL_checkstring(L, 1), nullptr,
                                                      static_cast<ImGuiWindowFlags>(luaL_optinteger(L, 2, 0))));
            return 1;
        },
        0);
    lua_setfield(L, t, "BeginPopupModal");

    // EndPopup()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::EndPopup();
            return 0;
        },
        0);
    lua_setfield(L, t, "EndPopup");

    // CloseCurrentPopup()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::CloseCurrentPopup();
            return 0;
        },
        0);
    lua_setfield(L, t, "CloseCurrentPopup");

    // Style
    // PushStyleColor(ImGuiCol, r, g, b, [a])
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::PushStyleColor(
                static_cast<ImGuiCol>(luaL_checkinteger(L, 1)),
                ImVec4(static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3)),
                       static_cast<float>(luaL_checknumber(L, 4)), static_cast<float>(luaL_optnumber(L, 5, 1.0))));
            return 0;
        },
        0);
    lua_setfield(L, t, "PushStyleColor");

    // PopStyleColor([count])
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::PopStyleColor(static_cast<int>(luaL_optinteger(L, 1, 1)));
            return 0;
        },
        0);
    lua_setfield(L, t, "PopStyleColor");

    // PushStyleVar(ImGuiStyleVar, value_or_x, [y]) -> pass y for Vec2 vars
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            auto idx = static_cast<ImGuiStyleVar>(luaL_checkinteger(L, 1));
            if (lua_type(L, 3) == LUA_TNUMBER) {
                ImGui::PushStyleVar(idx, ImVec2(static_cast<float>(luaL_checknumber(L, 2)),
                                                static_cast<float>(luaL_checknumber(L, 3))));
            } else {
                ImGui::PushStyleVar(idx, static_cast<float>(luaL_checknumber(L, 2)));
            }
            return 0;
        },
        0);
    lua_setfield(L, t, "PushStyleVar");

    // PopStyleVar([count])
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::PopStyleVar(static_cast<int>(luaL_optinteger(L, 1, 1)));
            return 0;
        },
        0);
    lua_setfield(L, t, "PopStyleVar");

    // Utilities
    // IsItemHovered() -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::IsItemHovered());
            return 1;
        },
        0);
    lua_setfield(L, t, "IsItemHovered");

    // IsItemClicked([button]) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::IsItemClicked(static_cast<ImGuiMouseButton>(luaL_optinteger(L, 1, 0))));
            return 1;
        },
        0);
    lua_setfield(L, t, "IsItemClicked");

    // IsWindowFocused() -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::IsWindowFocused());
            return 1;
        },
        0);
    lua_setfield(L, t, "IsWindowFocused");

    // IsWindowHovered() -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushboolean(L, ImGui::IsWindowHovered());
            return 1;
        },
        0);
    lua_setfield(L, t, "IsWindowHovered");

    // IsKeyPressed(key, [repeat]) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const ImGuiKey key = static_cast<ImGuiKey>(luaL_checkinteger(L, 1));
            const bool repeat = lua_toboolean(L, 2) != 0;
            lua_pushboolean(L, ImGui::IsKeyPressed(key, repeat));
            return 1;
        },
        0);
    lua_setfield(L, t, "IsKeyPressed");

    // SetTooltip(text)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::SetTooltip("%s", luaL_checkstring(L, 1));
            return 0;
        },
        0);
    lua_setfield(L, t, "SetTooltip");

    // Image / ImageButton
    // Image(texture, w, h)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            auto** ppTex = static_cast<VulkanTexture**>(luaL_checkudata(L, 1, "NST.Texture"));
            float w = static_cast<float>(luaL_checknumber(L, 2));
            float h = static_cast<float>(luaL_checknumber(L, 3));
            ImGui::Image((*ppTex)->textureRef(), ImVec2(w, h));
            return 0;
        },
        0);
    lua_setfield(L, t, "Image");

    // ImageButton(id, texture, w, h) -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const char* id = luaL_checkstring(L, 1);
            auto** ppTex = static_cast<VulkanTexture**>(luaL_checkudata(L, 2, "NST.Texture"));
            float w = static_cast<float>(luaL_checknumber(L, 3));
            float h = static_cast<float>(luaL_checknumber(L, 4));
            lua_pushboolean(L, ImGui::ImageButton(id, (*ppTex)->textureRef(), ImVec2(w, h)));
            return 1;
        },
        0);
    lua_setfield(L, t, "ImageButton");

    // Font / viewport helpers
    // SetNextWindowFullscreen() -> sets pos/size/viewport to main viewport
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->Pos);
            ImGui::SetNextWindowSize(vp->Size);
            ImGui::SetNextWindowViewport(vp->ID);
            return 0;
        },
        0);
    lua_setfield(L, t, "SetNextWindowFullscreen");

    // PushFont(font_lightuserdata)
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            if (lua_type(L, 1) == LUA_TLIGHTUSERDATA) {
                ImGui::PushFont(static_cast<ImFont*>(lua_touserdata(L, 1)));
            } else {
                ImGui::PushFont(nullptr);
            }
            return 0;
        },
        0);
    lua_setfield(L, t, "PushFont");

    // PopFont()
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImGui::PopFont();
            return 0;
        },
        0);
    lua_setfield(L, t, "PopFont");

    // CalcTextSize(text) -> w, h
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            ImVec2 sz = ImGui::CalcTextSize(luaL_checkstring(L, 1));
            lua_pushnumber(L, sz.x);
            lua_pushnumber(L, sz.y);
            return 2;
        },
        0);
    lua_setfield(L, t, "CalcTextSize");

    // GetWindowWidth() -> number
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushnumber(L, ImGui::GetWindowWidth());
            return 1;
        },
        0);
    lua_setfield(L, t, "GetWindowWidth");

    // GetWindowHeight() -> number
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            lua_pushnumber(L, ImGui::GetWindowHeight());
            return 1;
        },
        0);
    lua_setfield(L, t, "GetWindowHeight");

    lua_setglobal(L, "ImGui");

    // ImGuiWindowFlags constants
    lua_newtable(L);
    lua_pushinteger(L, ImGuiWindowFlags_None);
    lua_setfield(L, -2, "None");
    lua_pushinteger(L, ImGuiWindowFlags_NoTitleBar);
    lua_setfield(L, -2, "NoTitleBar");
    lua_pushinteger(L, ImGuiWindowFlags_NoResize);
    lua_setfield(L, -2, "NoResize");
    lua_pushinteger(L, ImGuiWindowFlags_NoMove);
    lua_setfield(L, -2, "NoMove");
    lua_pushinteger(L, ImGuiWindowFlags_NoScrollbar);
    lua_setfield(L, -2, "NoScrollbar");
    lua_pushinteger(L, ImGuiWindowFlags_NoScrollWithMouse);
    lua_setfield(L, -2, "NoScrollWithMouse");
    lua_pushinteger(L, ImGuiWindowFlags_NoCollapse);
    lua_setfield(L, -2, "NoCollapse");
    lua_pushinteger(L, ImGuiWindowFlags_AlwaysAutoResize);
    lua_setfield(L, -2, "AlwaysAutoResize");
    lua_pushinteger(L, ImGuiWindowFlags_NoBackground);
    lua_setfield(L, -2, "NoBackground");
    lua_pushinteger(L, ImGuiWindowFlags_NoSavedSettings);
    lua_setfield(L, -2, "NoSavedSettings");
    lua_pushinteger(L, ImGuiWindowFlags_MenuBar);
    lua_setfield(L, -2, "MenuBar");
    lua_pushinteger(L, ImGuiWindowFlags_NoNav);
    lua_setfield(L, -2, "NoNav");
    lua_pushinteger(L, ImGuiWindowFlags_NoDecoration);
    lua_setfield(L, -2, "NoDecoration");
    lua_pushinteger(L, ImGuiWindowFlags_NoBringToFrontOnFocus);
    lua_setfield(L, -2, "NoBringToFrontOnFocus");
    lua_setglobal(L, "ImGuiWindowFlags");

    // ImGuiCond constants
    lua_newtable(L);
    lua_pushinteger(L, ImGuiCond_None);
    lua_setfield(L, -2, "None");
    lua_pushinteger(L, ImGuiCond_Always);
    lua_setfield(L, -2, "Always");
    lua_pushinteger(L, ImGuiCond_Once);
    lua_setfield(L, -2, "Once");
    lua_pushinteger(L, ImGuiCond_FirstUseEver);
    lua_setfield(L, -2, "FirstUseEver");
    lua_pushinteger(L, ImGuiCond_Appearing);
    lua_setfield(L, -2, "Appearing");
    lua_setglobal(L, "ImGuiCond");

    // ImGuiCol constants
    lua_newtable(L);
    lua_pushinteger(L, ImGuiCol_Text);
    lua_setfield(L, -2, "Text");
    lua_pushinteger(L, ImGuiCol_TextDisabled);
    lua_setfield(L, -2, "TextDisabled");
    lua_pushinteger(L, ImGuiCol_WindowBg);
    lua_setfield(L, -2, "WindowBg");
    lua_pushinteger(L, ImGuiCol_ChildBg);
    lua_setfield(L, -2, "ChildBg");
    lua_pushinteger(L, ImGuiCol_PopupBg);
    lua_setfield(L, -2, "PopupBg");
    lua_pushinteger(L, ImGuiCol_Border);
    lua_setfield(L, -2, "Border");
    lua_pushinteger(L, ImGuiCol_FrameBg);
    lua_setfield(L, -2, "FrameBg");
    lua_pushinteger(L, ImGuiCol_FrameBgHovered);
    lua_setfield(L, -2, "FrameBgHovered");
    lua_pushinteger(L, ImGuiCol_FrameBgActive);
    lua_setfield(L, -2, "FrameBgActive");
    lua_pushinteger(L, ImGuiCol_TitleBg);
    lua_setfield(L, -2, "TitleBg");
    lua_pushinteger(L, ImGuiCol_TitleBgActive);
    lua_setfield(L, -2, "TitleBgActive");
    lua_pushinteger(L, ImGuiCol_Button);
    lua_setfield(L, -2, "Button");
    lua_pushinteger(L, ImGuiCol_ButtonHovered);
    lua_setfield(L, -2, "ButtonHovered");
    lua_pushinteger(L, ImGuiCol_ButtonActive);
    lua_setfield(L, -2, "ButtonActive");
    lua_pushinteger(L, ImGuiCol_Header);
    lua_setfield(L, -2, "Header");
    lua_pushinteger(L, ImGuiCol_HeaderHovered);
    lua_setfield(L, -2, "HeaderHovered");
    lua_pushinteger(L, ImGuiCol_HeaderActive);
    lua_setfield(L, -2, "HeaderActive");
    lua_pushinteger(L, ImGuiCol_Separator);
    lua_setfield(L, -2, "Separator");
    lua_pushinteger(L, ImGuiCol_CheckMark);
    lua_setfield(L, -2, "CheckMark");
    lua_pushinteger(L, ImGuiCol_SliderGrab);
    lua_setfield(L, -2, "SliderGrab");
    lua_pushinteger(L, ImGuiCol_SliderGrabActive);
    lua_setfield(L, -2, "SliderGrabActive");
    lua_setglobal(L, "ImGuiCol");

    // ImGuiStyleVar constants
    lua_newtable(L);
    lua_pushinteger(L, ImGuiStyleVar_Alpha);
    lua_setfield(L, -2, "Alpha");
    lua_pushinteger(L, ImGuiStyleVar_WindowPadding);
    lua_setfield(L, -2, "WindowPadding");
    lua_pushinteger(L, ImGuiStyleVar_WindowRounding);
    lua_setfield(L, -2, "WindowRounding");
    lua_pushinteger(L, ImGuiStyleVar_WindowBorderSize);
    lua_setfield(L, -2, "WindowBorderSize");
    lua_pushinteger(L, ImGuiStyleVar_FramePadding);
    lua_setfield(L, -2, "FramePadding");
    lua_pushinteger(L, ImGuiStyleVar_FrameRounding);
    lua_setfield(L, -2, "FrameRounding");
    lua_pushinteger(L, ImGuiStyleVar_ItemSpacing);
    lua_setfield(L, -2, "ItemSpacing");
    lua_pushinteger(L, ImGuiStyleVar_ItemInnerSpacing);
    lua_setfield(L, -2, "ItemInnerSpacing");
    lua_pushinteger(L, ImGuiStyleVar_IndentSpacing);
    lua_setfield(L, -2, "IndentSpacing");
    lua_pushinteger(L, ImGuiStyleVar_ScrollbarSize);
    lua_setfield(L, -2, "ScrollbarSize");
    lua_pushinteger(L, ImGuiStyleVar_GrabMinSize);
    lua_setfield(L, -2, "GrabMinSize");
    lua_pushinteger(L, ImGuiStyleVar_GrabRounding);
    lua_setfield(L, -2, "GrabRounding");
    lua_setglobal(L, "ImGuiStyleVar");

    // ImGuiKey constants
    lua_newtable(L);
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
        const ImGuiKey imguiKey = static_cast<ImGuiKey>(key);
        const char* keyName = ImGui::GetKeyName(imguiKey);
        if (keyName && keyName[0] != '\0') {
            lua_pushinteger(L, key);
            lua_setfield(L, -2, keyName);
        }
    }
    lua_setglobal(L, "ImGuiKey");

    // Texture userdata ("NST.Texture")
    // Full userdata holds a heap-allocated VulkanTexture*; __gc deletes it.
    luaL_newmetatable(L, "NST.Texture");
    int texMeta = lua_gettop(L);

    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            auto** ppTex = static_cast<VulkanTexture**>(lua_touserdata(L, 1));
            if (ppTex && *ppTex) {
                delete *ppTex;
                *ppTex = nullptr;
            }
            return 0;
        },
        0);
    lua_setfield(L, texMeta, "__gc");

    lua_newtable(L);
    int texMethods = lua_gettop(L);

    // texture:width() -> number
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            auto** ppTex = static_cast<VulkanTexture**>(luaL_checkudata(L, 1, "NST.Texture"));
            lua_pushnumber(L, (*ppTex)->width());
            return 1;
        },
        0);
    lua_setfield(L, texMethods, "width");

    // texture:height() -> number
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            auto** ppTex = static_cast<VulkanTexture**>(luaL_checkudata(L, 1, "NST.Texture"));
            lua_pushnumber(L, (*ppTex)->height());
            return 1;
        },
        0);
    lua_setfield(L, texMethods, "height");

    // texture:isValid() -> bool
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            auto** ppTex = static_cast<VulkanTexture**>(luaL_checkudata(L, 1, "NST.Texture"));
            lua_pushboolean(L, (*ppTex)->isValid());
            return 1;
        },
        0);
    lua_setfield(L, texMethods, "isValid");

    lua_setfield(L, texMeta, "__index"); // metatable.__index = methods table
    lua_pop(L, 1);                       // pop metatable

    // Texture global: Texture.load(VulkanContext, path) -> texture | nil, err
    lua_newtable(L);
    int texTable = lua_gettop(L);

    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            if (lua_type(L, 1) != LUA_TLIGHTUSERDATA) {
                return luaL_error(L, "Texture.load: arg 1 must be VulkanContext lightuserdata");
            }
            auto* ctx = static_cast<VulkanContext*>(lua_touserdata(L, 1));
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
        },
        0);
    lua_setfield(L, texTable, "load");

    lua_setglobal(L, "Texture");
}

} // namespace LuaStateBootstrap
