#include "scenes/SceneSharedState.hpp"

#include <sstream>

std::string modeLabel(const DisplayModeOption& mode) {
    std::ostringstream stream;
    stream << mode.width << " x " << mode.height << " @ " << mode.refreshRate << " Hz";
    return stream.str();
}
