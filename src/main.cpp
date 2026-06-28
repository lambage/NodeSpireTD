#include "Application.h"

#include <cstdio>


int main() {
    try {
        NST::Application app("NodeSpire TD", 1280, 720);
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Fatal] %s\n", e.what());
        return 1;
    }
    return 0;
}
