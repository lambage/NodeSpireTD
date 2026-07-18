#pragma once

struct lua_State;

class VulkanContext;

namespace LuaStateBootstrap {

// Initializes a scene Lua VM with all engine UI bindings and Vulkan context access.
void initializeEngineState(lua_State* L, const VulkanContext* context);

} // namespace LuaStateBootstrap
