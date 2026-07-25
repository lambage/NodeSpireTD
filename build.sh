#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

BUILD_DIR="${BUILD_DIR:-build}"
CMAKE_GENERATOR="${CMAKE_GENERATOR:-Ninja}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
CMAKE_CONFIGURE_ARGS="${CMAKE_CONFIGURE_ARGS:-}"
CMAKE_BUILD_ARGS="${CMAKE_BUILD_ARGS:-}"

print_usage() {
    cat <<'EOF'
Usage:
  ./build.sh [configure] [build] [clean] [help]

Examples:
  ./build.sh configure build
  ./build.sh configure
  ./build.sh build

Environment overrides:
  VULKAN_SDK_SETUP     Path to setup-env.sh (preferred if set)
  BUILD_DIR            CMake build directory (default: build)
  CMAKE_GENERATOR      CMake generator (default: Ninja)
  CMAKE_BUILD_TYPE     Build type for configure step (default: Release)
  CMAKE_CONFIGURE_ARGS Extra args passed to `cmake -B ...`
  CMAKE_BUILD_ARGS     Extra args passed to `cmake --build ...`
EOF
}

find_vulkan_setup_script() {
    if [[ -n "${VULKAN_SDK_SETUP:-}" && -f "${VULKAN_SDK_SETUP}" ]]; then
        printf '%s\n' "$VULKAN_SDK_SETUP"
        return 0
    fi

    if [[ -n "${VULKAN_SDK:-}" ]]; then
        local from_env
        from_env="$(dirname -- "$VULKAN_SDK")/setup-env.sh"
        if [[ -f "$from_env" ]]; then
            printf '%s\n' "$from_env"
            return 0
        fi
    fi

    if [[ -d "${HOME}/vulkan-sdk" ]]; then
        local latest
        latest="$(find "${HOME}/vulkan-sdk" -mindepth 2 -maxdepth 2 -type f -name setup-env.sh | sort -V | tail -n 1 || true)"
        if [[ -n "$latest" && -f "$latest" ]]; then
            printf '%s\n' "$latest"
            return 0
        fi
    fi

    return 1
}

setup_vulkan_env() {
    local setup_script
    if setup_script="$(find_vulkan_setup_script)"; then
        # Pass an explicit empty arg so SDK scripts that read $1 don't fail under nounset.
        # shellcheck source=/dev/null
        source "$setup_script" ""
        echo "[build.sh] Sourced Vulkan SDK environment: $setup_script"
    else
        echo "[build.sh] Warning: could not find Vulkan SDK setup-env.sh." >&2
        echo "[build.sh] Build may use system Vulkan headers/libraries instead of LunarG SDK." >&2
        echo "[build.sh] Set VULKAN_SDK_SETUP=/path/to/setup-env.sh to force a specific SDK." >&2
    fi
}

run_configure() {
    echo "[build.sh] Configuring in '$BUILD_DIR' (generator: $CMAKE_GENERATOR, type: $CMAKE_BUILD_TYPE)"
    if [[ -n "$CMAKE_CONFIGURE_ARGS" ]]; then
        cmake -B "$BUILD_DIR" -G "$CMAKE_GENERATOR" -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" $CMAKE_CONFIGURE_ARGS
    else
        cmake -B "$BUILD_DIR" -G "$CMAKE_GENERATOR" -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE"
    fi
}

run_build() {
    echo "[build.sh] Building '$BUILD_DIR'"
    if [[ -n "$CMAKE_BUILD_ARGS" ]]; then
        cmake --build "$BUILD_DIR" $CMAKE_BUILD_ARGS
    else
        cmake --build "$BUILD_DIR"
    fi
}

run_clean() {
    echo "[build.sh] Removing build directory '$BUILD_DIR'"
    rm -rf "$BUILD_DIR"
}

if [[ $# -eq 0 ]]; then
    set -- configure build
fi

setup_vulkan_env

for step in "$@"; do
    case "$step" in
        configure)
            run_configure
            ;;
        build)
            run_build
            ;;
        clean)
            run_clean
            ;;
        help|-h|--help)
            print_usage
            ;;
        *)
            echo "[build.sh] Unknown command: $step" >&2
            print_usage >&2
            exit 1
            ;;
    esac
done
