---
name: vendored-dependency-fact-check
description: 'Use before writing any code that calls into a CMake FetchContent-vendored library (RmlUi, SDL3, SFML, vk-bootstrap, VMA, fastgltf, protobuf, Boost, etc.) whenever the exact API shape (constructor signature, header path, CMake target name, enum value) is not already confirmed in this session. Prevents hallucinated API calls by locating and reading the real vendored source instead of relying on memory of a library''s public docs or a different version.'
---

# Vendored Dependency Fact-Check

NodeSpireTD pulls every third-party dependency via `FetchContent` in the root
[CMakeLists.txt](../../../CMakeLists.txt) — nothing is a system package. This means:

- The exact API surface is whatever git tag is pinned, which may differ from the latest
  docs/README you remember.
- Headers/library CMake targets are not on disk until the project has been configured at least
  once (`cmake --preset ...` or equivalent). If a `_deps` folder doesn't exist yet, say so instead
  of guessing paths.

## Procedure

1. **Find the pinned version first.** Grep [CMakeLists.txt](../../../CMakeLists.txt) for the
   `FetchContent_Declare(<name> ... GIT_TAG ...)` block. The `GIT_TAG` is the ground truth for
   which API you're allowed to assume — not "latest main" from memory.
2. **Locate the vendored source.** After configure, sources land in
   `build/_deps/<name>-src/` and the generated build target lives under `build/_deps/<name>-build/`.
   Use `list_dir` / `file_search` on `build/_deps/` to confirm the folder exists before reading from it.
   - If the dependency hasn't been added to `CMakeLists.txt` yet (e.g. RmlUi, SDL3 at the start of
     this migration), there is nothing to read — say that explicitly and propose the
     `FetchContent_Declare` block first, modeled on an existing entry (imgui/spdlog/glm are good
     templates: `GIT_REPOSITORY` + pinned `GIT_TAG` + `GIT_SHALLOW ON`).
3. **Grep the real source for the symbol you're about to use** (constructor, function, macro,
   CMake target name) instead of writing it from memory. For CMake target names specifically,
   check the vendored `CMakeLists.txt`'s `add_library(...)`/`install(TARGETS ...)` calls — the
   FetchContent target name doesn't always match the repo name (e.g. `luasrc` vs `lua`, see the
   bottom of the root CMakeLists.txt where the `lua` target is hand-assembled from `luasrc_SOURCE_DIR`).
4. **If the source truly can't be located** (not fetched yet, private repo, docs-only claim),
   say exactly that to the user instead of presenting a guess as fact. Offer to add the
   `FetchContent_Declare` entry so it becomes available, then re-check.
5. **Cache what you learn for the session.** Once a signature/path is confirmed, don't re-derive
   it — but do re-verify if the git tag changes or a new session starts and enough time has passed
   that the pin may have moved.

## Why this matters for this migration specifically

RmlUi's SDL+Vulkan backend and Lua plugin are far less commonly seen in training data than Dear
ImGui's — the mode's own constraints call out not inventing their API details from memory. Treat
every RmlUi/SDL3 header, class, and constructor as unverified until grepped from
`build/_deps/rmlui-src` / `build/_deps/sdl-src` (once added).
