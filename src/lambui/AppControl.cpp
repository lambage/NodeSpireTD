#include "lambui/AppControl.hpp"

namespace NodeSpireUi {

namespace {
bool g_quitRequested = false;
}

void RequestQuit() {
    g_quitRequested = true;
}

bool QuitRequested() {
    return g_quitRequested;
}

} // namespace NodeSpireUi
