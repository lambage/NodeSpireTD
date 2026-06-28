#include "Application.h"

#include <cstdio>

static int runApp() {
    try {
        NST::Application app("NodeSpire TD", 1280, 720);
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Fatal] %s\n", e.what());
        return 1;
    }
    return 0;
}

int main() {
    return runApp();
}

#if defined(_WIN32)
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return runApp();
}
#endif
