#include "lambui/LuaAudioBindings.hpp"

#include "AudioEngine.hpp"
#include "lua.hpp"

#include <string_view>

namespace NodeSpireUi {

namespace {

AudioEngine& Self(lua_State* L) {
    return *static_cast<AudioEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
}

AudioChannel ParseChannel(lua_State* L, int index) {
    const char* name = luaL_checkstring(L, index);
    if (std::string_view{name} == "Music") return AudioChannel::Music;
    return AudioChannel::Sfx;
}

int LuaPlay(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    const AudioChannel channel = ParseChannel(L, 2);
    const bool loop = lua_isboolean(L, 3) ? (lua_toboolean(L, 3) != 0) : false;
    const float gain = lua_isnumber(L, 4) ? static_cast<float>(lua_tonumber(L, 4)) : 1.0f;
    Self(L).play(path, channel, loop, gain);
    return 0;
}

int LuaPreload(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    const AudioChannel channel = ParseChannel(L, 2);
    Self(L).preload(path, channel);
    return 0;
}

int LuaRelease(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    const AudioChannel channel = ParseChannel(L, 2);
    Self(L).release(path, channel);
    return 0;
}

void PushAudioFunction(lua_State* L, AudioEngine& audio, lua_CFunction fn, const char* name) {
    lua_pushlightuserdata(L, &audio);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

} // namespace

void BindAudioEngine(lua_State* L, AudioEngine& audio) {
    lua_newtable(L);
    PushAudioFunction(L, audio, LuaPlay, "Play");
    PushAudioFunction(L, audio, LuaPreload, "Preload");
    PushAudioFunction(L, audio, LuaRelease, "Release");
    lua_setglobal(L, "Audio");
}

} // namespace NodeSpireUi
