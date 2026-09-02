# NodeSpireTD

Cross-platform tower defense project scaffolded for C++20 with SFML,
using a Vulkan-first renderer setup.

## Goals

- Keep development workflow consistent between Windows and Linux.
- Provide a minimal `NodeSpireTDGame` executable to validate toolchain setup.

## Prerequisites

- CMake >= 3.24
- Git
- Ninja
- C++ toolchain:
	- Windows: Visual Studio 2022 Build Tools (MSVC)
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

Run from the build tree:

- Windows: `build/windows-dev/Debug/NodeSpireTDGame.exe`
- Linux: `build/linux-dev/NodeSpireTDGame`


# Known Issues

- logs are spammy, we should turn off trace logging and debug logging by default (perhaps a -v for debug and --extra-verbose for trace). rotating logs would also be a plus
- client UI's need to show wave timer and other game state that is shown on the host
- when selecting another players tower we should use a different color and make it obvious that the player cannot do upgrades/sell etc...
- host was rejecting client placing a tower because the host didn't have enough money but the client did
- no chat/whisper feature, would be nice to have a WoW style command prompt/chat
- window resize crashes vulkan
- would rather have a party system than the current host starts map and client connects
- should probably just pick a port number and not allow users to modify it, may also need some additional data in the initial handshake so we can reject clients trying to connect to that port on a machine that might be running a different service and not get cross talk
