#pragma once

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace NodeSpireUi {

// Verbose scene-lifecycle tracing, gated behind the NODESPIRE_UI_TRACE=1
// environment variable (checked once, cached for the process lifetime).
// Covers SceneManager transitions/layout changes and the LuaUiScene
// OnEnter/OnUpdate/OnLayoutChanged/OnExit dispatch, to make it easy to see
// what's actually reaching each scene's Lua script.
inline bool UiTraceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("NODESPIRE_UI_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

template <typename... Args>
void UiTrace(const char* format, Args&&... args) {
    if (!UiTraceEnabled()) {
        return;
    }
    std::fprintf(stderr, "[UI TRACE] ");
    std::fprintf(stderr, format, std::forward<Args>(args)...);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

} // namespace NodeSpireUi
