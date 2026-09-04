#include "multiplayer/ChatCommandDispatcher.hpp"

#include "multiplayer/PartyProtocol.hpp"

#include <algorithm>
#include <cctype>

namespace multiplayer {
namespace {

std::string toLower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // namespace

ChatCommandDispatcher::ChatCommandDispatcher() {
    // Register new emotes/commands here -- one line each, no other code needs to change.
    handlers_.emplace("roar", [](std::string_view speaker, std::string_view) {
        return std::string(speaker) + " roars!";
    });
    handlers_.emplace("wave", [](std::string_view speaker, std::string_view) {
        return std::string(speaker) + " waves.";
    });
    handlers_.emplace("dance", [](std::string_view speaker, std::string_view) {
        return std::string(speaker) + " dances.";
    });
    handlers_.emplace("me", [](std::string_view speaker, std::string_view args) {
        return args.empty() ? std::string(speaker) : std::string(speaker) + " " + std::string(args);
    });
}

std::string ChatCommandDispatcher::sanitize(std::string_view rawText) {
    std::string result;
    result.reserve(rawText.size());
    for (const char c : rawText) {
        // Strip control characters (CR/LF/NUL/escape, etc.) so a chat line can never smuggle
        // terminal escapes or spoof multi-line/system-looking content; ordinary whitespace and
        // printable characters pass through untouched.
        const auto uc = static_cast<unsigned char>(c);
        if (uc == '\t' || (uc >= 0x20 && uc != 0x7F)) {
            result.push_back(c);
        }
    }
    if (result.size() > kMaxPartyChatMessageLength) {
        result.resize(kMaxPartyChatMessageLength);
    }

    const auto first = result.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = result.find_last_not_of(" \t");
    return result.substr(first, last - first + 1);
}

ChatDispatchResult ChatCommandDispatcher::process(std::string_view rawText, std::string_view speakerDisplayName) const {
    const std::string sanitized = sanitize(rawText);
    if (sanitized.empty()) {
        return {};
    }

    if (sanitized.front() != '/') {
        return ChatDispatchResult{ChatOutcome{sanitized, false}, std::nullopt};
    }

    const std::string withoutSlash = sanitized.substr(1);
    const auto spacePos = withoutSlash.find(' ');
    const std::string commandName =
        toLower(spacePos == std::string::npos ? withoutSlash : withoutSlash.substr(0, spacePos));
    const std::string_view args =
        spacePos == std::string::npos ? std::string_view{} : std::string_view(withoutSlash).substr(spacePos + 1);

    if (commandName.empty()) {
        // A lone "/" with nothing after it: treat as plain text rather than an error.
        return ChatDispatchResult{ChatOutcome{sanitized, false}, std::nullopt};
    }

    const auto handlerIt = handlers_.find(commandName);
    if (handlerIt == handlers_.end()) {
        return ChatDispatchResult{std::nullopt, "Unknown command: /" + commandName};
    }

    return ChatDispatchResult{ChatOutcome{handlerIt->second(speakerDisplayName, args), true}, std::nullopt};
}

} // namespace multiplayer
