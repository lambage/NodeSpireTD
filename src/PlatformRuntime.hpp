#pragma once

#include "IAchievementService.hpp"
#include "ISaveSystem.hpp"

#include "lua.hpp"

#include <lua.h>
#include <memory>

class PlatformRuntime {
  public:
    PlatformRuntime(lua_State* L);
    ~PlatformRuntime();
    void tick();

    [[nodiscard]] IAchievementService& achievements();
    [[nodiscard]] ISaveSystem& saves();

  private:
    std::unique_ptr<IAchievementService> achievementService_;
    std::unique_ptr<ISaveSystem> saveSystem_;
    lua_State* L_ = nullptr;
};
