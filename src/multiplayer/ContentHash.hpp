#pragma once

#include <string>
#include <vector>

namespace multiplayer {

// Deterministic, order-independent digest over a set of content identifiers (e.g. every loaded
// tower/enemy archetype id). Not cryptographic and not tamper-proof -- it only needs to differ
// whenever a host and a joining peer have installed different gameplay content, so
// JoinMatchRequest::contentManifest can catch that mismatch during the join handshake.
std::string computeContentDigest(std::vector<std::string> ids);

} // namespace multiplayer
