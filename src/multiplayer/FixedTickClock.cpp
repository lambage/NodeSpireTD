#include "multiplayer/FixedTickClock.hpp"

#include <algorithm>
#include <cmath>

namespace multiplayer {

std::uint32_t FixedTickClock::consumeTicks(float elapsedSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0f) {
        return 0;
    }

    accumulatedSeconds_ += std::min(elapsedSeconds, kMaxFrameSeconds);
    const std::uint32_t availableTicks = static_cast<std::uint32_t>(accumulatedSeconds_ / kTickSeconds);
    const std::uint32_t consumedTicks = std::min(availableTicks, kMaxTicksPerFrame);
    accumulatedSeconds_ -= static_cast<float>(consumedTicks) * kTickSeconds;
    return consumedTicks;
}

void FixedTickClock::reset() {
    accumulatedSeconds_ = 0.0f;
}

} // namespace multiplayer