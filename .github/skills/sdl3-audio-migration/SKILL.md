---
name: sdl3-audio-migration
description: 'Use when replacing AudioEngine''s SFML-based music/sfx playback (sf::Music, sf::Sound, sf::SoundBuffer) with an SDL3-based audio solution (SDL3 audio subsystem and/or SDL3_mixer). Covers preserving AudioEngine''s existing public interface (preload/release/play/update/activeAssetKeys) so GameButton, GameImageButton, and Lua bindings don''t need to change, plus the CMake and API differences to verify.'
---

# SDL3 Audio Migration (from SFML)

## Scope
[src/AudioEngine.hpp](../../../src/AudioEngine.hpp) / [.cpp](../../../src/AudioEngine.cpp) is the
only audio-owning class. Its private members are the SFML-specific part:
`std::unique_ptr<sf::Music>` per `MusicPlayback`, `std::shared_ptr<sf::SoundBuffer>` +
`std::unique_ptr<sf::Sound>` per `SfxPlayback`, plus a `sfxBufferCache_` keyed by path.

**Consumers to leave untouched:** [src/utility/GameButton.hpp](../../../src/utility/GameButton.hpp)
and `GameImageButton` take `AudioEngine&` and call its public methods with hover/click sound paths;
Lua-facing scene code likely calls into `AudioEngine` indirectly through scene controllers. As long
as the public API (`preload`, `release`, `play`, `update`, `activeAssetKeys`, `setEffectiveSettings`)
keeps its current signatures and semantics, none of those call sites need to change — this is an
internal implementation swap, not an interface redesign.

## Decision: SDL3 audio subsystem vs SDL3_mixer
- Raw `SDL_AudioStream`/`SDL_OpenAudioDeviceStream` (SDL3 audio subsystem) gives low-level PCM
  control but you'd have to hand-roll format decoding (ogg/mp3/wav), looping, per-sound gain
  mixing, and simultaneous-sound mixing yourself — SFML's `sf::Music`/`sf::Sound` did all of that.
- `SDL3_mixer` (the SDL3-compatible fork of SDL_mixer) provides `Mix_Music`/`Mix_Chunk` with
  built-in decoding (via vendored libvorbis/libFLAC/etc., which this repo's `_deps` already
  fetches transitively for SFML — check whether SDL3_mixer needs its own copies), looping, and
  channel-based simultaneous playback — much closer to a drop-in replacement for the current
  `MusicPlayback`/`SfxPlayback` shape. Default to SDL3_mixer unless the user says otherwise;
  confirm its exact CMake target/API via the
  [vendored-dependency-fact-check](../vendored-dependency-fact-check/SKILL.md) skill before writing
  code, since SDL3_mixer's C API and CMake integration are less commonly seen in training data
  than SDL_mixer 2.x.

## Mapping sketch (verify each against vendored headers)
| Current (SFML) | SDL3_mixer equivalent (verify) |
|---|---|
| `sf::Music` (streamed, one active track typically) | `Mix_Music*` via `Mix_LoadMUS` / `Mix_PlayMusic` / `Mix_HaltMusic` |
| `sf::SoundBuffer` (decoded buffer cache) | `Mix_Chunk*` via `Mix_LoadWAV` (works for common formats once mixer is built with the right decoders) |
| `sf::Sound` (playing instance of a buffer) | channel handle returned by `Mix_PlayChannel(-1, chunk, loops)` |
| `sound.setVolume(...)` / `music.setVolume(...)` (0-100 in SFML) | `Mix_VolumeChunk`/`Mix_VolumeMusic` (0-`MIX_MAX_VOLUME`, typically 0-128) — **scale factor differs, don't assume 0-100** |
| looping via `sf::Music::setLoop(true)` | `Mix_PlayMusic(music, -1)` (loops param, `-1` = infinite) vs `Mix_PlayChannel(chan, chunk, loops)` |

## Procedure
1. Add the SDL3_mixer `FetchContent` block (pattern-matched against existing entries in
   [CMakeLists.txt](../../../CMakeLists.txt)); confirm it depends on SDL3 already being fetched
   (order matters for `FetchContent_MakeAvailable`).
2. Reimplement `MusicPlayback`/`SfxPlayback` internals in [AudioEngine.cpp](../../../src/AudioEngine.cpp)
   against `Mix_*` calls; keep the forward-declared `sf::Music`/`sf::Sound`/`sf::SoundBuffer` names
   out of the header once replaced (swap for `Mix_Music`/`Mix_Chunk` forward decls or an opaque
   pimpl if the mixer types can't be forward-declared cleanly).
3. Re-verify `setEffectiveSettings`'s volume math (master/mute mixing) still produces the same
   perceptual result given SDL3_mixer's 0-128 volume range instead of SFML's 0-100.
4. Confirm `update(float dt)`'s age-tracking/pruning logic (used to expire finished one-shot sfx)
   maps onto `Mix_Playing(channel)`/callback-based "channel finished" hooks rather than SFML's
   `sound.getStatus() == sf::SoundSource::Status::Stopped` poll.
5. Leave `GameButton`/`GameImageButton`/Lua call sites alone unless their build breaks — the goal is
   zero call-site changes outside `AudioEngine`.
