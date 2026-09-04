#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace multiplayer {

// A single formatted line ready to relay to the whole party.
struct ChatOutcome {
    std::string text;
    // True for emote-formatted lines (e.g. "PlayerX roars") so clients can style them distinctly
    // from plain chat.
    bool isEmote = false;
};

struct ChatDispatchResult {
    // Set => relay this line to every party member (including the sender).
    std::optional<ChatOutcome> broadcast;
    // Set => send this feedback back to the sender only; never relayed to the party.
    std::optional<std::string> localError;
};

// Extensible slash-command parser for party chat. Plain text is relayed as-is; a "/command args"
// line is looked up by name in a small dispatch table and turned into a formatted broadcast line
// (WoW/IRC-style), or a sender-only error if the command is unrecognized. Add a new emote/command
// by registering another entry in the constructor's table -- no branching if/else chain required.
//
// Purely cosmetic/social: handlers only ever produce display text and must never be given access
// to gameplay state, match commands, or player accounts.
class ChatCommandDispatcher {
  public:
    ChatCommandDispatcher();

    // rawText is untrusted network input: this enforces the party chat length limit and strips
    // control characters before any parsing happens. Returns an empty result (nothing to
    // broadcast, no error) for text that sanitizes down to nothing.
    ChatDispatchResult process(std::string_view rawText, std::string_view speakerDisplayName) const;

  private:
    // (speakerDisplayName, command arguments) -> formatted broadcast text.
    using Handler = std::function<std::string(std::string_view speakerDisplayName, std::string_view args)>;

    static std::string sanitize(std::string_view rawText);

    std::unordered_map<std::string, Handler> handlers_;
};

} // namespace multiplayer
