#pragma once

#include "lua.hpp"

#include <stdexcept>

class AppController {
  public:
    AppController() : L_(luaL_newstate()) {
        if (!L_) {
            throw std::runtime_error("Failed to create Lua state");
        }
    }
    ~AppController() {
        if (L_) {
            lua_close(L_);
        }
    }

    int run();

  private:
    lua_State* L_ = nullptr;
};
