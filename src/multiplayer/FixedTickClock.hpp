#pragma once

#include <cstdint>

namespace multiplayer {

class FixedTickClock {
  public:
    static constexpr float kTickSeconds = 1.0f / 30.0f;
    static constexpr std::uint32_t kMaxTicksPerFrame = 8;
    static constexpr float kMaxFrameSeconds = kTickSeconds * static_cast<float>(kMaxTicksPerFrame);

    std::uint32_t consumeTicks(float elapsedSeconds);
    void reset();

  private:
    float accumulatedSeconds_ = 0.0f;
};

} // namespace multiplayer