# NodeSpireTD

Cross-platform C++20 tower defense project using SDL3, RmlUi, and a Vulkan-first renderer.

## Goals

- Keep development workflow consistent between Windows and Linux.
- Build the `NodeSpireTD` game and its multiplayer/RmlUi test suite.

## Prerequisites

- CMake >= 3.24
- Git
- Ninja
- C++ toolchain:
	- Windows: Visual Studio 2022 Build Tools (MSVC). Run the Ninja configure and build commands
	  from **Developer PowerShell for VS 2022** so MSVC's standard-library include paths are set.
	- Linux: GCC or Clang

### Option 1: Through CMake target

Configure your project once:

```bash
cmake -S . -B build -G Ninja
```

Then run:

```bash
cmake --build build
```

Run from the build tree or install it first:

- Windows: `build/src/NodeSpireTD.exe`
- Install: `cmake --install build`, then run `build/install/NodeSpireTD.exe`

The default log level is `info`; `-v`/`--verbose` enables debug and
`--extra-verbose` enables trace. Logs rotate at `logs/nodespiretd.log` (5 MB x 3 files).


# Known Issues

- no chat/whisper feature, would be nice to have a WoW style command prompt/chat
- would rather have a party system than the current host starts map and client connects
- should probably just pick a port number and not allow users to modify it, may also need some additional data in the initial handshake so we can reject clients trying to connect to that port on a machine that might be running a different service and not get cross talk

