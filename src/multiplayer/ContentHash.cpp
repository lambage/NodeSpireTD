#include "multiplayer/ContentHash.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace multiplayer {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t fnv1a(std::string_view data, std::uint64_t hash) {
    for (unsigned char byte : data) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace

std::string computeContentDigest(std::vector<std::string> ids) {
    std::sort(ids.begin(), ids.end());

    std::uint64_t hash = kFnvOffsetBasis;
    for (const std::string& id : ids) {
        hash = fnv1a(id, hash);
        hash = fnv1a(std::string_view("\0", 1), hash); // separator: distinguishes {"ab","c"} from {"a","bc"}
    }

    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

} // namespace multiplayer
