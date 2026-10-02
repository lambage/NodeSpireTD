#pragma once

struct lua_State;
class AudioEngine;

namespace NodeSpireUi {

// Exposes a global "Audio" table to the given Lua state:
//   Audio.Play(path, channel, loop, gain)   -- channel: "Music" or "Sfx"; loop/gain optional
//   Audio.Preload(path, channel)
//   Audio.Release(path, channel)
// so scene scripts can trigger UI sfx (button hover/click) and background
// music directly from widget SetScript callbacks, replacing the old
// GameButton/GameImageButton built-in sfx wrappers.
void BindAudioEngine(lua_State* L, AudioEngine& audio);

} // namespace NodeSpireUi
