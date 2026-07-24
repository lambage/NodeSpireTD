#pragma once

struct lua_State;

class VulkanContext;
class AudioEngine;

namespace LuaStateBootstrap {

// Initializes a scene Lua VM with all engine UI bindings and Vulkan context/audio access.
void initializeEngineState(lua_State* L, const VulkanContext* context, AudioEngine* audioEngine);

} // namespace LuaStateBootstrap
